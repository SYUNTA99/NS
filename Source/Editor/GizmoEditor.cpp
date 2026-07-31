#include "Editor/GizmoEditor.h"

#include "Editor/GridMath.h"
#include "Runtime/Math/Math.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Platform/Input.h"
#include "Runtime/Platform/Keyboard.h"
#include "Runtime/Platform/Mouse.h"
#include "Runtime/UI/ImGuiContext.h"

#include <limits>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{
    namespace
    {
        constexpr float k_MoveSnapStep = 0.5f;

        // 回転スナップの刻み幅 (15度)
        constexpr float k_RotateSnapStep = NS::Math::k_Pi / 12.0f;

        constexpr float k_ScaleSnapStep = 0.25f;
        constexpr float k_ScaleSensitivity = 0.01f;

        // スケールの最小値
        constexpr float k_ScaleMin = 0.01f;

        // ワールド空間におけるギズモハンドルの基本長
        constexpr float k_HandleLength = 1.0f;

        // ピッキング判定時のスクリーン上の許容半径（ピクセル単位）
        constexpr float k_PickThresholdPixels = 12.0f;

        // 画面でこれより短い矢印は掴む対象にしない（ピクセル単位）
        constexpr float k_MinHandlePixels = 8.0f;

        // 掴んだ点からの距離がこの差に収まる矢印は同じ近さとみなす（ピクセル単位）
        constexpr float k_PickTieMarginPixels = 3.0f;

        // 視線と操作軸が平行とみなす閾値
        constexpr float k_AxisViewParallelEpsilon = 0.999f;

        // レイと平面が平行とみなす閾値
        constexpr float k_PlaneParallelEpsilon = 1e-6f;

        // 回転操作を無視する、原点からの最小距離の二乗
        constexpr float k_RingGrabRadiusEpsilonSq = 1e-6f;

        // AABB交差判定時の平行閾値
        constexpr float k_RayAabbParallelEpsilon = 1e-8f;

        // 指定された刻み幅で値をスナップ（丸め）する
        [[nodiscard]] float SnapTo(float value, float step) noexcept
        {
            if (step <= 0.0f)
            {
                return value;
            }
            return std::round(value / step) * step;
        }

        [[nodiscard]] NS::Math::Vector3 AxisVector(GizmoAxis axis) noexcept
        {
            switch (axis)
            {
            case GizmoAxis::X:
                return {1.0f, 0.0f, 0.0f};
            case GizmoAxis::Y:
                return {0.0f, 1.0f, 0.0f};
            case GizmoAxis::Z:
                return {0.0f, 0.0f, 1.0f};
            default:
                return {0.0f, 0.0f, 0.0f};
            }
        }

        // 指定された軸ベクトルを、与えられた回転クォータニオンで変換して返す
        [[nodiscard]] NS::Math::Vector3 OrientedAxis(GizmoAxis axis, const NS::Math::Quaternion& rotation) noexcept
        {
            return NS::Math::Vector3::Transform(AxisVector(axis), rotation);
        }

        // ツールと座標系モードに基づき、ギズモハンドルの基準となる回転を決定する
        [[nodiscard]] NS::Math::Quaternion EffectiveAxisOrientation(GizmoTool tool,
                                                                    GizmoSpace space,
                                                                    const NS::Math::Quaternion& objectRotation) noexcept
        {
            if (tool == GizmoTool::Scale)
            {
                return objectRotation;
            }
            if (space == GizmoSpace::World)
            {
                return NS::Math::Quaternion::Identity;
            }
            return objectRotation;
        }

        // レイと平面の交点を算出する。
        [[nodiscard]] bool IntersectRayWithPlane(const NS::Math::Ray& ray,
                                                 const NS::Math::Vector3& planePoint,
                                                 const NS::Math::Vector3& planeNormal,
                                                 NS::Math::Vector3& outPoint) noexcept
        {
            const float denom = ray.direction.Dot(planeNormal);
            if (std::fabs(denom) < k_PlaneParallelEpsilon)
            {
                return false;
            }
            const float t = (planePoint - ray.position).Dot(planeNormal) / denom;
            outPoint = ray.position + ray.direction * t;
            return true;
        }

        // レイと原点中心のAABBとの交差判定を行う
        [[nodiscard]] bool IntersectRayCenteredAabb(const NS::Math::Vector3& origin,
                                                    const NS::Math::Vector3& direction,
                                                    const NS::Math::Vector3& halfExtents,
                                                    float& outT) noexcept
        {
            const float o[3] = {origin.x, origin.y, origin.z};
            const float d[3] = {direction.x, direction.y, direction.z};
            const float he[3] = {halfExtents.x, halfExtents.y, halfExtents.z};

            float tMin = -std::numeric_limits<float>::infinity();
            float tMax = std::numeric_limits<float>::infinity();
            for (int axis = 0; axis < 3; ++axis)
            {
                if (std::fabs(d[axis]) < k_RayAabbParallelEpsilon)
                {
                    if (o[axis] < -he[axis] || o[axis] > he[axis])
                    {
                        return false;
                    }
                    continue;
                }
                const float invD = 1.0f / d[axis];
                float t1 = (-he[axis] - o[axis]) * invD;
                float t2 = (he[axis] - o[axis]) * invD;
                if (t1 > t2)
                {
                    std::swap(t1, t2);
                }
                tMin = std::max(t1, tMin);
                tMax = std::min(t2, tMax);
                if (tMin > tMax)
                {
                    return false;
                }
            }
            if (tMax < 0.0f)
            {
                return false;
            }

            if (tMin >= 0.0f)
            {
                outT = tMin;
            }
            else
            {
                outT = tMax;
            }
            return true;
        }

        // ワールド座標をスクリーン座標へ投影する
        [[nodiscard]] bool ProjectToScreen(const NS::Math::Vector3& world,
                                           const NS::Math::Matrix& vp,
                                           NS::Math::Size2D viewport,
                                           NS::Math::Vector2& outScreen) noexcept
        {
            const NS::Math::Vector4 worldH{world.x, world.y, world.z, 1.0f};
            const NS::Math::Vector4 clip = NS::Math::Vector4::Transform(worldH, vp);
            if (clip.w <= 0.0f)
            {
                return false;
            }
            outScreen.x = ((clip.x / clip.w) * 0.5f + 0.5f) * static_cast<float>(viewport.width);
            outScreen.y = (1.0f - ((clip.y / clip.w) * 0.5f + 0.5f)) * static_cast<float>(viewport.height);
            return true;
        }

        // 画面上での見かけの大きさを一定に保つための、ハンドルのワールド空間での長さを算出する
        [[nodiscard]] float HandleWorldLength(const NS::Math::Vector3& origin, const NS::Math::Matrix& vp) noexcept
        {
            const NS::Math::Vector4 clip =
                NS::Math::Vector4::Transform(NS::Math::Vector4{origin.x, origin.y, origin.z, 1.0f}, vp);

            if (clip.w <= 1.0e-3f)
            {
                return k_HandleLength;
            }
            const float scale = clip.w / 10.0f;
            return k_HandleLength * std::max(scale, 1.0f);
        }

        // 点から線分までの最短距離を算出する
        [[nodiscard]] float DistancePointToSegment(NS::Math::Vector2 p,
                                                   NS::Math::Vector2 a,
                                                   NS::Math::Vector2 b) noexcept
        {
            const float abx = b.x - a.x;
            const float aby = b.y - a.y;
            const float apx = p.x - a.x;
            const float apy = p.y - a.y;
            const float lenSq = abx * abx + aby * aby;
            float t = 0.0f;
            if (lenSq > 1e-12f)
            {
                t = (apx * abx + apy * aby) / lenSq;
            }
            if (t < 0.0f)
            {
                t = 0.0f;
            }
            else if (t > 1.0f)
            {
                t = 1.0f;
            }
            const float cx = a.x + abx * t;
            const float cy = a.y + aby * t;
            const float dx = p.x - cx;
            const float dy = p.y - cy;
            return std::sqrt(dx * dx + dy * dy);
        }

        void ApplyState(NS::Object::Transform& target, const TransformState& state) noexcept
        {
            target.SetPosition(state.position);
            target.SetRotation(state.rotation);
            target.SetScale(state.scale);
        }

        // スクリーンのドラッグ操作を評価し、新しいトランスフォーム状態を計算する
        [[nodiscard]] TransformState ComputeDragResult(const TransformState& before,
                                                       GizmoTool tool,
                                                       GizmoSpace space,
                                                       GizmoAxis axis,
                                                       const NS::Math::Matrix& viewProjection,
                                                       NS::Math::Size2D viewport,
                                                       NS::Math::Vector2 screenStart,
                                                       NS::Math::Vector2 screenEnd,
                                                       bool snap) noexcept
        {
            TransformState after = before;
            const NS::Math::Quaternion axisOrient = EffectiveAxisOrientation(tool, space, before.rotation);
            switch (tool)
            {
            case GizmoTool::Move:
            {
                const NS::Math::Ray rayStart = NS::Editor::ScreenToWorldRay(
                    viewProjection, viewport, static_cast<int>(screenStart.x), static_cast<int>(screenStart.y));
                const NS::Math::Ray rayNow = NS::Editor::ScreenToWorldRay(
                    viewProjection, viewport, static_cast<int>(screenEnd.x), static_cast<int>(screenEnd.y));
                after.position =
                    GizmoEditor::ComputeAxisMove(before.position, axis, axisOrient, rayStart, rayNow, snap);
                break;
            }
            case GizmoTool::Rotate:
            {
                const float angle = GizmoEditor::WorldDragToAngle(
                    before.position, axis, axisOrient, viewProjection, viewport, screenStart, screenEnd);
                after.rotation =
                    GizmoEditor::ComputeAxisRotate(before.rotation, axis, angle, snap, space == GizmoSpace::World);
                break;
            }
            case GizmoTool::Scale:
            {
                NS::Math::Vector2 axisDir2d{};
                if (axis != GizmoAxis::Uniform)
                {
                    NS::Math::Vector2 origin2d{};
                    NS::Math::Vector2 end2d{};
                    const NS::Math::Vector3 axisEnd = before.position + OrientedAxis(axis, axisOrient) * k_HandleLength;
                    if (ProjectToScreen(before.position, viewProjection, viewport, origin2d) &&
                        ProjectToScreen(axisEnd, viewProjection, viewport, end2d))
                    {
                        axisDir2d = end2d - origin2d;
                    }
                }
                const NS::Math::Vector2 dragPixels = screenEnd - screenStart;
                const float amount = GizmoEditor::ScreenDragToScaleAmount(axisDir2d, dragPixels);
                after.scale = GizmoEditor::ComputeScale(before.scale, axis, amount, snap);
                break;
            }
            case GizmoTool::Select:
            default:
                break;
            }
            return after;
        }

        // 指定軸に直交するリング（円）上の座標を算出する
        [[nodiscard]] NS::Math::Vector3 RingPoint(GizmoAxis axis,
                                                  const NS::Math::Vector3& center,
                                                  float t,
                                                  const NS::Math::Quaternion& rotation,
                                                  float radius) noexcept
        {
            NS::Math::Vector3 u{};
            NS::Math::Vector3 v{};
            switch (axis)
            {
            case GizmoAxis::X:
                u = NS::Math::Vector3{0.0f, 1.0f, 0.0f};
                v = NS::Math::Vector3{0.0f, 0.0f, 1.0f};
                break;
            case GizmoAxis::Y:
                u = NS::Math::Vector3{1.0f, 0.0f, 0.0f};
                v = NS::Math::Vector3{0.0f, 0.0f, 1.0f};
                break;
            case GizmoAxis::Z:
                u = NS::Math::Vector3{1.0f, 0.0f, 0.0f};
                v = NS::Math::Vector3{0.0f, 1.0f, 0.0f};
                break;
            default:
                return center;
            }
            u = NS::Math::Vector3::Transform(u, rotation);
            v = NS::Math::Vector3::Transform(v, rotation);
            const float c = std::cos(t) * radius;
            const float s = std::sin(t) * radius;
            return center + u * c + v * s;
        }

        // スクリーン上における、指定したリングとマウス座標との最短距離を算出する
        [[nodiscard]] float DistanceToRing(GizmoAxis axis,
                                           const NS::Math::Vector3& center,
                                           const NS::Math::Quaternion& rotation,
                                           const NS::Math::Matrix& vp,
                                           NS::Math::Size2D viewport,
                                           NS::Math::Vector2 mouse2d) noexcept
        {
            constexpr int k_Segments = 32;
            const float radius = HandleWorldLength(center, vp);
            float best = 1.0e30f;
            NS::Math::Vector2 prev{};
            bool prevValid = false;
            for (int i = 0; i <= k_Segments; ++i)
            {
                const float t = (2.0f * NS::Math::k_Pi * static_cast<float>(i)) / static_cast<float>(k_Segments);
                NS::Math::Vector2 screen{};
                const bool ok = ProjectToScreen(RingPoint(axis, center, t, rotation, radius), vp, viewport, screen);
                if (ok && prevValid)
                {
                    const float d = DistancePointToSegment(mouse2d, prev, screen);
                    if (d < best)
                    {
                        best = d;
                    }
                }
                prev = screen;
                prevValid = ok;
            }
            return best;
        }
    } // namespace

    void GizmoEditor::SetSelectableObjects(std::span<NS::Object::GameObject* const> objects,
                                           std::span<const NS::Math::Vector3> localHalfExtents,
                                           std::span<const std::uint8_t> pickable) noexcept
    {
        m_objects = objects;
        m_halfExtents = localHalfExtents;
        m_pickable = pickable;
    }

    void GizmoEditor::Tick(const NS::Math::Matrix& viewProjection, const ViewRect& view) noexcept
    {
        if (!m_active || m_input == nullptr)
        {
            return;
        }

        NS::Platform::Keyboard& kb = m_input->Keyboard();
        NS::Platform::Mouse& mouse = m_input->Mouse();

        const bool imguiWantsKeyboard = (m_imgui != nullptr) && m_imgui->WantCaptureKeyboard();

        // 右ドラッグ中の W/A/S/D/Q/E は編集カメラの移動なので、 ツール切替に食われないようにする
        const bool cameraFlying = mouse.IsHeld(NS::Platform::MouseButton::Right);

        // 入力によるツールの切り替え処理
        if (!imguiWantsKeyboard && !m_dragging && !cameraFlying)
        {
            if (kb.IsPressed(NS::Platform::Key::Q))
            {
                OnToolKey(NS::Platform::Key::Q);
            }
            if (kb.IsPressed(NS::Platform::Key::W))
            {
                OnToolKey(NS::Platform::Key::W);
            }
            if (kb.IsPressed(NS::Platform::Key::E))
            {
                OnToolKey(NS::Platform::Key::E);
            }
            if (kb.IsPressed(NS::Platform::Key::R))
            {
                OnToolKey(NS::Platform::Key::R);
            }
            if (kb.IsPressed(NS::Platform::Key::X))
            {
                ToggleSpace();
            }
        }

        // マウスはパネル基準のローカル座標で扱う。継続中のドラッグは矩形外でも従来どおり動かす
        const NS::Math::Size2D viewport = ViewRectSize(view);
        int viewMouseX = 0;
        int viewMouseY = 0;
        WindowMouseToViewSpace(mouse.GetX(), mouse.GetY(), viewMouseX, viewMouseY);
        int localX = 0;
        int localY = 0;
        ViewRectToLocal(view, viewMouseX, viewMouseY, localX, localY);
        const NS::Math::Vector2 mouse2d{static_cast<float>(localX), static_cast<float>(localY)};
        const bool snap = kb.IsHeld(NS::Platform::Key::Ctrl);

        // ドラッグ中の変形処理
        if (m_dragging)
        {
            if (m_selected != nullptr && mouse.IsHeld(NS::Platform::MouseButton::Left))
            {
                const TransformState preview = ComputeDragResult(m_dragBefore,
                                                                 m_tool,
                                                                 m_space,
                                                                 m_dragAxis,
                                                                 viewProjection,
                                                                 viewport,
                                                                 m_dragStartScreen,
                                                                 mouse2d,
                                                                 snap);
                ApplyState(*m_selected, preview);
            }
            else
            {
                // ドラッグ終了処理
                m_dragging = false;
                m_dragAxis = GizmoAxis::None;
            }
            return;
        }

        if (!mouse.IsPressed(NS::Platform::MouseButton::Left))
        {
            return;
        }

        // クリック開始 (ハンドル掴み・選択) はパネル上でだけ受ける。Game 窓の上では ImGui が常に
        // マウスを要求するため、UI との取り合いは hover (他窓が上に無い) と矩形内で判定する
        if (!m_viewHovered || !ViewRectContains(view, viewMouseX, viewMouseY))
        {
            return;
        }

        // ギズモハンドルのピッキング判定を行う
        if (m_selected != nullptr && m_tool != GizmoTool::Select)
        {
            const GizmoAxis axis = ToolHandlePick(m_selected->Position(),
                                                  EffectiveAxisOrientation(m_tool, m_space, m_selected->Rotation()),
                                                  m_tool,
                                                  mouse2d,
                                                  viewProjection,
                                                  viewport);
            if (axis != GizmoAxis::None)
            {
                m_dragging = true;
                m_dragAxis = axis;
                m_dragStartScreen = mouse2d;
                m_dragBefore = TransformState{m_selected->Position(), m_selected->Rotation(), m_selected->Scale()};
                return;
            }
        }

        // オブジェクトのピッキング判定を行う
        const NS::Math::Ray ray = NS::Editor::ScreenToWorldRay(viewProjection, viewport, localX, localY);
        std::vector<NS::Math::Matrix> worldMatrices;
        worldMatrices.reserve(m_objects.size());
        for (const NS::Object::GameObject* obj : m_objects)
        {
            worldMatrices.push_back(obj->Root().WorldMatrix());
        }

        // 可視オブジェクトを優先して判定し、ヒットしなければ不可視オブジェクトも含めて再判定する
        int hit = PickNearestObb(ray, worldMatrices, m_halfExtents, m_pickable);
        if (hit < 0)
        {
            hit = PickNearestObb(ray, worldMatrices, m_halfExtents);
        }

        if (hit >= 0)
        {
            m_selected = &m_objects[static_cast<std::size_t>(hit)]->Root();
        }
        else
        {
            m_selected = nullptr;
        }
    }

    void GizmoEditor::Render(const NS::Math::Matrix& viewProjection, const ViewRect& view) noexcept
    {
#if NS_EDITOR_ENABLED
        if (!m_active || m_selected == nullptr)
        {
            return;
        }
        if (view.width <= 0 || view.height <= 0)
        {
            return;
        }

        // 中央に Game 窓が立つため背景 drawlist では窓の裏に隠れる。前面へ描き、パネル外はクリップで漏らさない
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        if (dl == nullptr)
        {
            return;
        }

        const NS::Math::Size2D viewport = ViewRectSize(view);
        const float panelX = static_cast<float>(view.x);
        const float panelY = static_cast<float>(view.y);
        // ProjectToScreen はパネルローカルを返すので、drawlist へ渡す直前に原点を加算する
        const auto toPx = [panelX, panelY](NS::Math::Vector2 local) -> ImVec2 {
            return ImVec2{panelX + local.x, panelY + local.y};
        };

        // 選択ツールは何も出さない。掴む物が無い状態で印だけ置いても操作の手がかりにならない
        if (m_tool == GizmoTool::Select)
        {
            return;
        }

        const NS::Math::Vector3 origin = m_selected->Position();
        const NS::Math::Quaternion rotation = EffectiveAxisOrientation(m_tool, m_space, m_selected->Rotation());
        NS::Math::Vector2 origin2d{};
        if (!ProjectToScreen(origin, viewProjection, viewport, origin2d))
        {
            return;
        }

        dl->PushClipRect(ImVec2{panelX, panelY},
                         ImVec2{panelX + static_cast<float>(view.width), panelY + static_cast<float>(view.height)},
                         true);

        const ImVec2 originPx = toPx(origin2d);

        // 現在の座標系（Local / World）をテキスト描画する
        const bool worldEffective = (m_tool != GizmoTool::Scale) && (m_space == GizmoSpace::World);
        const char* spaceLabel = "Local";
        if (worldEffective)
        {
            spaceLabel = "World";
        }
        dl->AddText(ImVec2{originPx.x + 12.0f, originPx.y + 8.0f}, IM_COL32(235, 235, 235, 255), spaceLabel);

        const ImU32 axisColors[3] = {
            IM_COL32(230, 70, 70, 255),
            IM_COL32(70, 220, 90, 255),
            IM_COL32(90, 140, 255, 255),
        };
        const GizmoAxis axes[3] = {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};

        const float handleLength = HandleWorldLength(origin, viewProjection);

        if (m_tool == GizmoTool::Rotate)
        {
            // 回転ツール時のリングを描画する
            constexpr int k_Segments = 48;
            for (int a = 0; a < 3; ++a)
            {
                ImVec2 prev{};
                bool prevValid = false;
                for (int i = 0; i <= k_Segments; ++i)
                {
                    const float t = (2.0f * NS::Math::k_Pi * static_cast<float>(i)) / static_cast<float>(k_Segments);
                    NS::Math::Vector2 screen{};
                    const bool ok = ProjectToScreen(
                        RingPoint(axes[a], origin, t, rotation, handleLength), viewProjection, viewport, screen);
                    const ImVec2 cur = toPx(screen);
                    if (ok && prevValid)
                    {
                        dl->AddLine(prev, cur, axisColors[a], 2.0f);
                    }
                    prev = cur;
                    prevValid = ok;
                }
            }
        }
        else
        {
            // 移動・スケールツール時の軸線と端点を描画する
            for (int i = 0; i < 3; ++i)
            {
                const NS::Math::Vector3 dir = OrientedAxis(axes[i], rotation);
                const NS::Math::Vector3 endWorld{
                    origin.x + dir.x * handleLength,
                    origin.y + dir.y * handleLength,
                    origin.z + dir.z * handleLength,
                };
                NS::Math::Vector2 end2d{};
                if (!ProjectToScreen(endWorld, viewProjection, viewport, end2d))
                {
                    continue;
                }

                const ImVec2 endPx = toPx(end2d);
                dl->AddLine(originPx, endPx, axisColors[i], 2.5f);

                if (m_tool == GizmoTool::Scale)
                {
                    constexpr float k_BoxHalf = 4.0f;
                    dl->AddRectFilled(ImVec2{endPx.x - k_BoxHalf, endPx.y - k_BoxHalf},
                                      ImVec2{endPx.x + k_BoxHalf, endPx.y + k_BoxHalf},
                                      axisColors[i]);
                }
                else
                {
                    // 移動ハンドルの矢印を描画する
                    const float dx = endPx.x - originPx.x;
                    const float dy = endPx.y - originPx.y;
                    const float len = std::sqrt(dx * dx + dy * dy);
                    if (len > 1.0e-3f)
                    {
                        constexpr float k_HeadLength = 13.0f;
                        constexpr float k_HeadHalfWidth = 5.0f;
                        const float ux = dx / len;
                        const float uy = dy / len;
                        const ImVec2 baseLeft{endPx.x - ux * k_HeadLength - uy * k_HeadHalfWidth,
                                              endPx.y - uy * k_HeadLength + ux * k_HeadHalfWidth};
                        const ImVec2 baseRight{endPx.x - ux * k_HeadLength + uy * k_HeadHalfWidth,
                                               endPx.y - uy * k_HeadLength - ux * k_HeadHalfWidth};
                        dl->AddTriangleFilled(endPx, baseLeft, baseRight, axisColors[i]);
                    }
                    else
                    {
                        // 軸が視線と平行な場合は円として描画する
                        dl->AddCircleFilled(endPx, 5.0f, axisColors[i]);
                    }
                }
            }

            // スケールの中央ハンドル（Uniform）を描画する
            if (m_tool == GizmoTool::Scale)
            {
                constexpr float k_CenterHalf = 5.0f;
                dl->AddRectFilled(ImVec2{originPx.x - k_CenterHalf, originPx.y - k_CenterHalf},
                                  ImVec2{originPx.x + k_CenterHalf, originPx.y + k_CenterHalf},
                                  IM_COL32(235, 235, 235, 255));
            }
        }

        dl->PopClipRect();
#else
        (void)viewProjection;
        (void)view;
#endif
    }

    GizmoTool GizmoEditor::ToolForKey(GizmoTool current, NS::Platform::Key key) noexcept
    {
        switch (key)
        {
        case NS::Platform::Key::Q:
            return GizmoTool::Select;
        case NS::Platform::Key::W:
            return GizmoTool::Move;
        case NS::Platform::Key::E:
            return GizmoTool::Rotate;
        case NS::Platform::Key::R:
            return GizmoTool::Scale;
        default:
            return current;
        }
    }

    int GizmoEditor::PickNearestObb(const NS::Math::Ray& ray,
                                    std::span<const NS::Math::Matrix> worldMatrices,
                                    std::span<const NS::Math::Vector3> localHalfExtents,
                                    std::span<const std::uint8_t> pickMask) noexcept
    {
        // 対象オブジェクト群に対してレイキャストを行い、最近接のインデックスを特定する
        const std::size_t count = std::min(worldMatrices.size(), localHalfExtents.size());
        int best = -1;
        float bestT = 0.0f;
        for (std::size_t i = 0; i < count; ++i)
        {
            // マスクで無効化されたオブジェクトをスキップする
            if (!pickMask.empty() && (i >= pickMask.size() || pickMask[i] == 0))
            {
                continue;
            }

            const NS::Math::Matrix inv = worldMatrices[i].Invert();
            const NS::Math::Vector3 localOrigin = NS::Math::Vector3::Transform(ray.position, inv);
            const NS::Math::Vector3 localDir = NS::Math::Vector3::TransformNormal(ray.direction, inv);
            float t = 0.0f;
            if (IntersectRayCenteredAabb(localOrigin, localDir, localHalfExtents[i], t) && (best < 0 || t < bestT))
            {
                best = static_cast<int>(i);
                bestT = t;
            }
        }
        return best;
    }

    NS::Math::Vector3 GizmoEditor::ComputeAxisMove(const NS::Math::Vector3& startPos,
                                                   GizmoAxis axis,
                                                   const NS::Math::Quaternion& rotation,
                                                   const NS::Math::Ray& rayStart,
                                                   const NS::Math::Ray& rayNow,
                                                   bool snap) noexcept
    {
        if (axis != GizmoAxis::X && axis != GizmoAxis::Y && axis != GizmoAxis::Z)
        {
            return startPos;
        }

        const NS::Math::Vector3 a = OrientedAxis(axis, rotation);

        // 視線と操作軸が平行に近い場合は移動をキャンセルする
        NS::Math::Vector3 viewDir = rayNow.direction;
        viewDir.Normalize();
        if (std::fabs(a.Dot(viewDir)) >= k_AxisViewParallelEpsilon)
        {
            return startPos;
        }

        // 軸を含み、視線に最も正対する平面の法線を算出する
        NS::Math::Vector3 normal = a.Cross(viewDir.Cross(a));
        if (normal.LengthSquared() < k_PlaneParallelEpsilon)
        {
            return startPos;
        }
        normal.Normalize();

        NS::Math::Vector3 p0{};
        NS::Math::Vector3 p1{};
        if (!IntersectRayWithPlane(rayStart, startPos, normal, p0) ||
            !IntersectRayWithPlane(rayNow, startPos, normal, p1))
        {
            return startPos;
        }

        float delta = (p1 - p0).Dot(a);

        // 算出した移動量をスナップして適用する
        if (snap)
        {
            delta = SnapTo(delta, k_MoveSnapStep);
        }
        return startPos + a * delta;
    }

    float GizmoEditor::WorldDragToAngle(const NS::Math::Vector3& origin,
                                        GizmoAxis axis,
                                        const NS::Math::Quaternion& rotation,
                                        const NS::Math::Matrix& viewProjection,
                                        NS::Math::Size2D viewport,
                                        NS::Math::Vector2 screenStart,
                                        NS::Math::Vector2 screenEnd) noexcept
    {
        const NS::Math::Vector3 n = OrientedAxis(axis, rotation);
        if (n.LengthSquared() < 0.5f)
        {
            return 0.0f;
        }

        const NS::Math::Ray rayStart = NS::Editor::ScreenToWorldRay(
            viewProjection, viewport, static_cast<int>(screenStart.x), static_cast<int>(screenStart.y));
        const NS::Math::Ray rayNow = NS::Editor::ScreenToWorldRay(
            viewProjection, viewport, static_cast<int>(screenEnd.x), static_cast<int>(screenEnd.y));

        // 軸直交平面との交点を算出し、ワールド座標上の操作点を特定する
        NS::Math::Vector3 hitStart{};
        NS::Math::Vector3 hitNow{};
        if (!IntersectRayWithPlane(rayStart, origin, n, hitStart) || !IntersectRayWithPlane(rayNow, origin, n, hitNow))
        {
            return 0.0f;
        }

        const NS::Math::Vector3 v0 = hitStart - origin;
        const NS::Math::Vector3 v1 = hitNow - origin;
        if (v0.LengthSquared() < k_RingGrabRadiusEpsilonSq || v1.LengthSquared() < k_RingGrabRadiusEpsilonSq)
        {
            return 0.0f;
        }

        // 操作点の前回・今回のベクトルから、軸周りの回転角を算出する
        const float sinComponent = v0.Cross(v1).Dot(n);
        const float cosComponent = v0.Dot(v1);
        return std::atan2(sinComponent, cosComponent);
    }

    NS::Math::Quaternion GizmoEditor::ComputeAxisRotate(
        const NS::Math::Quaternion& startRot, GizmoAxis axis, float angleRad, bool snap, bool worldSpace) noexcept
    {
        if (axis != GizmoAxis::X && axis != GizmoAxis::Y && axis != GizmoAxis::Z)
        {
            return startRot;
        }

        const float angle = [&]() -> float {
            if (snap)
            {
                return SnapTo(angleRad, k_RotateSnapStep);
            }
            return angleRad;
        }();

        // 回転軸を決定する
        const NS::Math::Vector3 n = [&]() -> NS::Math::Vector3 {
            if (worldSpace)
            {
                return AxisVector(axis);
            }
            return OrientedAxis(axis, startRot);
        }();

        const NS::Math::Quaternion delta = NS::Math::Quaternion::CreateFromAxisAngle(n, angle);

        // 回転を合成して返す
        return startRot * delta;
    }

    float GizmoEditor::ScreenDragToScaleAmount(NS::Math::Vector2 axisDir2d, NS::Math::Vector2 dragPixels) noexcept
    {
        // 操作軸が視線と平行な場合は、スクリーンのX軸移動量を基準にフォールバックする
        const float axisLen = axisDir2d.Length();
        if (axisLen <= 1.0e-6f)
        {
            const float sign = [&]() -> float {
                if (dragPixels.x < 0.0f)
                {
                    return -1.0f;
                }
                return 1.0f;
            }();
            return dragPixels.Length() * k_ScaleSensitivity * sign;
        }

        const NS::Math::Vector2 axisUnit{axisDir2d.x / axisLen, axisDir2d.y / axisLen};
        return dragPixels.Dot(axisUnit) * k_ScaleSensitivity;
    }

    NS::Math::Vector3 GizmoEditor::ComputeScale(const NS::Math::Vector3& startScale,
                                                GizmoAxis axis,
                                                float amount,
                                                bool snap) noexcept
    {
        NS::Math::Vector3 result = startScale;
        switch (axis)
        {
        case GizmoAxis::X:
            result.x = startScale.x + amount;
            break;
        case GizmoAxis::Y:
            result.y = startScale.y + amount;
            break;
        case GizmoAxis::Z:
            result.z = startScale.z + amount;
            break;
        case GizmoAxis::Uniform:
            result.x = startScale.x + amount;
            result.y = startScale.y + amount;
            result.z = startScale.z + amount;
            break;
        case GizmoAxis::None:
        default:
            return startScale;
        }

        if (snap)
        {
            result.x = SnapTo(result.x, k_ScaleSnapStep);
            result.y = SnapTo(result.y, k_ScaleSnapStep);
            result.z = SnapTo(result.z, k_ScaleSnapStep);
        }

        // スケール値が最小値を下回らないようにクランプする
        result.x = std::max(result.x, k_ScaleMin);
        result.y = std::max(result.y, k_ScaleMin);
        result.z = std::max(result.z, k_ScaleMin);
        return result;
    }

    GizmoAxis GizmoEditor::ToolHandlePick(const NS::Math::Vector3& gizmoOrigin,
                                          const NS::Math::Quaternion& rotation,
                                          GizmoTool tool,
                                          NS::Math::Vector2 mouse2d,
                                          const NS::Math::Matrix& viewProjection,
                                          NS::Math::Size2D viewport) noexcept
    {
        // 選択ツール時はハンドルのピッキングを行わない
        if (tool == GizmoTool::Select)
        {
            return GizmoAxis::None;
        }
        if (viewport.width <= 0 || viewport.height <= 0)
        {
            return GizmoAxis::None;
        }

        NS::Math::Vector2 origin2d{};
        if (!ProjectToScreen(gizmoOrigin, viewProjection, viewport, origin2d))
        {
            return GizmoAxis::None;
        }

        // 回転リングのピッキング判定を行う
        if (tool == GizmoTool::Rotate)
        {
            const GizmoAxis ringAxes[3] = {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};
            GizmoAxis bestRing = GizmoAxis::None;
            float bestRingDistance = k_PickThresholdPixels;
            for (int i = 0; i < 3; ++i)
            {
                const float d = DistanceToRing(ringAxes[i], gizmoOrigin, rotation, viewProjection, viewport, mouse2d);
                if (d < bestRingDistance)
                {
                    bestRingDistance = d;
                    bestRing = ringAxes[i];
                }
            }
            return bestRing;
        }

        // スケールツールのUniformハンドル（中央）を優先的に判定する
        if (tool == GizmoTool::Scale)
        {
            const float dx = mouse2d.x - origin2d.x;
            const float dy = mouse2d.y - origin2d.y;
            if (std::sqrt(dx * dx + dy * dy) < k_PickThresholdPixels)
            {
                return GizmoAxis::Uniform;
            }
        }

        const float handleLength = HandleWorldLength(gizmoOrigin, viewProjection);
        const GizmoAxis axisEnum[3] = {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};

        GizmoAxis best = GizmoAxis::None;
        float bestDistance = k_PickThresholdPixels;
        float bestPixels = 0.0f;
        for (int i = 0; i < 3; ++i)
        {
            const NS::Math::Vector3 dir = OrientedAxis(axisEnum[i], rotation);
            const NS::Math::Vector3 endWorld{
                gizmoOrigin.x + dir.x * handleLength,
                gizmoOrigin.y + dir.y * handleLength,
                gizmoOrigin.z + dir.z * handleLength,
            };
            NS::Math::Vector2 end2d{};

            // 背面にある軸をスキップする
            if (!ProjectToScreen(endWorld, viewProjection, viewport, end2d))
            {
                continue;
            }

            // 視線と重なって画面で潰れた矢印は掴めても引けないので候補から外す
            const float ax = end2d.x - origin2d.x;
            const float ay = end2d.y - origin2d.y;
            const float pixels = std::sqrt(ax * ax + ay * ay);
            if (pixels < k_MinHandlePixels)
            {
                continue;
            }

            const float dist = DistancePointToSegment(mouse2d, origin2d, end2d);
            if (dist >= k_PickThresholdPixels)
            {
                continue;
            }
            if (best == GizmoAxis::None)
            {
                best = axisEnum[i];
                bestDistance = dist;
                bestPixels = pixels;
                continue;
            }

            // 重なった矢印は短い方を採る。長い方は短い方の先端より外で掴めるので両方に手が届く
            const bool closer = dist < bestDistance - k_PickTieMarginPixels;
            const bool tied = std::fabs(dist - bestDistance) <= k_PickTieMarginPixels;
            if (closer || (tied && pixels < bestPixels))
            {
                best = axisEnum[i];
                bestDistance = std::min(dist, bestDistance);
                bestPixels = pixels;
            }
        }

        return best;
    }

    void GizmoEditor::ApplyDragForTest(const NS::Math::Matrix& viewProjection,
                                       NS::Math::Size2D viewport,
                                       GizmoAxis axis,
                                       NS::Math::Vector2 screenStart,
                                       NS::Math::Vector2 screenEnd) noexcept
    {
        if (m_selected == nullptr)
        {
            return;
        }

        const TransformState before{m_selected->Position(), m_selected->Rotation(), m_selected->Scale()};
        const TransformState after =
            ComputeDragResult(before, m_tool, m_space, axis, viewProjection, viewport, screenStart, screenEnd, false);
        ApplyState(*m_selected, after);
    }

    void GizmoEditor::OnToolKey(NS::Platform::Key key) noexcept
    {
        m_tool = ToolForKey(m_tool, key);
    }

} // namespace NS::Editor