#include "Editor/GizmoEditor.h"

#include "Editor/GridMath.h"
#include "Framework/Platform/Input.h"
#include "Framework/Platform/Mouse.h"
#include "Framework/Scene/Components/EditorCameraComponent.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Transform.h"
#include "Framework/UI/ImGuiContext.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{
    namespace
    {
        // ワールド単位の移動スナップの刻み
        constexpr float kMoveSnapStep = 0.5f;

        // 回転スナップの刻みで 15 度 = π/12 rad
        constexpr float kRotateSnapStep = NS::Math::kPi / 12.0f;

        // スケールスナップの刻み
        constexpr float kScaleSnapStep = 0.25f;

        // ドラッグ 1px あたりのスケール変化量
        constexpr float kScaleSensitivity = 0.01f;

        // 0 以下に潰れると mesh が反転/消失するので下限を張る
        constexpr float kScaleMin = 0.01f;

        // world でのハンドル軸の長さ。 origin から各軸方向にこの距離だけ伸ばした端点を picking に使う
        constexpr float kHandleLength = 1.0f;

        // px 単位の screen 上のヒット許容半径。 これ未満の最近接軸を採用する
        constexpr float kPickThresholdPixels = 12.0f;

        // 軸と視線がこれ以上そろうと、 軸を含む平面が薄くなって交点が暴れるので何もしないようにする
        constexpr float kAxisViewParallelEpsilon = 0.999f;

        // ray と平面の交差判定で、 分母 rayDir・n がこの値未満なら平行とみなし交差不能
        constexpr float kPlaneParallelEpsilon = 1e-6f;

        // 回転で掴んだ点が origin に近すぎて半径ほぼ 0 だと角度が暴れるので無視する閾値で world 距離の二乗
        constexpr float kRingGrabRadiusEpsilonSq = 1e-6f;

        // スラブ判定で方向成分がこの絶対値未満なら、 その軸に平行とみなす
        constexpr float kRayAabbParallelEpsilon = 1e-8f;

        // 最近接の step 倍へ丸める。 step<=0 は素通し
        [[nodiscard]] float SnapTo(float value, float step) noexcept
        {
            return (step <= 0.0f) ? value : std::round(value / step) * step;
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

        // 選択物の local 軸を world 方向で返す。 ハンドル・移動・回転・スケールを local 座標系に
        // 揃える単一の窓口で、 rotation が identity なら従来の world 軸と一致する
        [[nodiscard]] NS::Math::Vector3 OrientedAxis(GizmoAxis axis, const NS::Math::Quaternion& rotation) noexcept
        {
            return NS::Math::Vector3::Transform(AxisVector(axis), rotation);
        }

        // ハンドル軸を置く座標系の回転を返す。 Scale は常に local の objectRotation、 Move/Rotate は space に従い
        // World なら identity で world 軸、 Local なら objectRotation。 描画・ピック・ドラッグ算出が共有する
        [[nodiscard]] NS::Math::Quaternion EffectiveAxisOrientation(GizmoTool tool,
                                                                    GizmoSpace space,
                                                                    const NS::Math::Quaternion& objectRotation) noexcept
        {
            if (tool == GizmoTool::Scale)
                return objectRotation;
            return (space == GizmoSpace::World) ? NS::Math::Quaternion::Identity : objectRotation;
        }

        // startPos を通り axis を含む法線 planeNormal の平面と ray の交点。 交差不能なら false
        [[nodiscard]] bool IntersectRayWithPlane(const NS::Math::Ray& ray,
                                                 const NS::Math::Vector3& planePoint,
                                                 const NS::Math::Vector3& planeNormal,
                                                 NS::Math::Vector3& outPoint) noexcept
        {
            const float denom = ray.direction.Dot(planeNormal);
            if (std::fabs(denom) < kPlaneParallelEpsilon)
                return false;
            const float t = (planePoint - ray.position).Dot(planeNormal) / denom;
            outPoint = ray.position + ray.direction * t;
            return true;
        }

        // ray と中心原点 AABB のスラブ判定。 返す t は ray パラメータで方向のスケールを保つので
        // object 間でそのまま大小比較できる。 SimpleMath の Ray::Intersects は方向が単位ベクトル
        // である assert を持つが、 ここは逆変換後の非単位方向を渡すので自前で判定する
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
                if (std::fabs(d[axis]) < kRayAabbParallelEpsilon)
                {
                    if (o[axis] < -he[axis] || o[axis] > he[axis])
                        return false; // 軸に平行かつスラブ外
                    continue;
                }
                const float invD = 1.0f / d[axis];
                float t1 = (-he[axis] - o[axis]) * invD;
                float t2 = (he[axis] - o[axis]) * invD;
                if (t1 > t2)
                    std::swap(t1, t2);
                tMin = (t1 > tMin) ? t1 : tMin;
                tMax = (t2 < tMax) ? t2 : tMax;
                if (tMin > tMax)
                    return false;
            }
            if (tMax < 0.0f)
                return false; // box は ray の後方

            outT = (tMin >= 0.0f) ? tMin : tMax; // origin が box 外なら入口、 内なら出口
            return true;
        }

        // world 点を screen へ投影する。 clip.w<=0 のカメラ背面は false。 式は RenderCursorPreview と同一
        [[nodiscard]] bool ProjectToScreen(const NS::Math::Vector3& world,
                                           const NS::Math::Matrix& vp,
                                           NS::Math::Size2D viewport,
                                           NS::Math::Vector2& outScreen) noexcept
        {
            const NS::Math::Vector4 worldH{world.x, world.y, world.z, 1.0f};
            const NS::Math::Vector4 clip = NS::Math::Vector4::Transform(worldH, vp);
            if (clip.w <= 0.0f)
                return false;
            outScreen.x = ((clip.x / clip.w) * 0.5f + 0.5f) * static_cast<float>(viewport.width);
            outScreen.y = (1.0f - ((clip.y / clip.w) * 0.5f + 0.5f)) * static_cast<float>(viewport.height);
            return true;
        }

        // 選択物がカメラから遠いほどハンドルの world 長を伸ばし、 screen 上の見かけ寸法を一定に近づける
        // screen 寸法は world 長 / clip.w に比例するので、 world 長を clip.w に比例させると相殺されて一定になる
        // 近距離は kHandleLength を下限に据え、 遠距離だけ伸ばす
        [[nodiscard]] float HandleWorldLength(const NS::Math::Vector3& origin, const NS::Math::Matrix& vp) noexcept
        {
            const NS::Math::Vector4 clip =
                NS::Math::Vector4::Transform(NS::Math::Vector4{origin.x, origin.y, origin.z, 1.0f}, vp);
            // clip.w がほぼ 0、 カメラ至近や背面で深度が信頼できない時は深度で割らず固定長へ退避する
            if (clip.w <= 1.0e-3f)
                return kHandleLength;
            // 深度 10 までは kHandleLength のまま、 これより遠い選択物ほど深度に比例して伸ばし画面上一定に近づける
            const float scale = clip.w / 10.0f;
            return kHandleLength * ((scale > 1.0f) ? scale : 1.0f);
        }

        // 点 p から線分 a-b への px 単位の最短距離。 線分が a==b に縮退したら点 a への距離
        [[nodiscard]] float DistancePointToSegment(NS::Math::Vector2 p,
                                                   NS::Math::Vector2 a,
                                                   NS::Math::Vector2 b) noexcept
        {
            const float abx = b.x - a.x;
            const float aby = b.y - a.y;
            const float apx = p.x - a.x;
            const float apy = p.y - a.y;
            const float lenSq = abx * abx + aby * aby;
            float t = (lenSq > 1e-12f) ? ((apx * abx + apy * aby) / lenSq) : 0.0f;
            if (t < 0.0f)
                t = 0.0f;
            else if (t > 1.0f)
                t = 1.0f;
            const float cx = a.x + abx * t;
            const float cy = a.y + aby * t;
            const float dx = p.x - cx;
            const float dy = p.y - cy;
            return std::sqrt(dx * dx + dy * dy);
        }

        // PRS を Transform へ書き戻す
        void ApplyState(NS::Scene::Transform& target, const TransformState& state) noexcept
        {
            target.SetPosition(state.position);
            target.SetRotation(state.rotation);
            target.SetScale(state.scale);
        }

        // screenStart→screenEnd のドラッグを現ツール / 軸の新 PRS へ変換する
        // Tick のライブプレビューと ApplyDragForTest が同じ算出を共有する
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
            // ハンドル軸を置く座標系。 Move/Rotate は space に従い、 Scale は常に local
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
                    const NS::Math::Vector3 axisEnd = before.position + OrientedAxis(axis, axisOrient) * kHandleLength;
                    if (ProjectToScreen(before.position, viewProjection, viewport, origin2d) &&
                        ProjectToScreen(axisEnd, viewProjection, viewport, end2d))
                        axisDir2d = end2d - origin2d;
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

        // rotation で回した local 軸 axis 周りの半径 kHandleLength のリング上の点で、 軸直交平面内の角度 t
        // 軸直交平面の 2 基底 u/v を rotation で回すことで、 リングが選択物の傾きに追従する
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

        // リングを screen 折れ線に投影し mouse2d との最短距離を px で返す。 背面に回った区間は除外する
        [[nodiscard]] float DistanceToRing(GizmoAxis axis,
                                           const NS::Math::Vector3& center,
                                           const NS::Math::Quaternion& rotation,
                                           const NS::Math::Matrix& vp,
                                           NS::Math::Size2D viewport,
                                           NS::Math::Vector2 mouse2d) noexcept
        {
            constexpr int kSegments = 32;
            const float radius = HandleWorldLength(center, vp);
            float best = 1.0e30f;
            NS::Math::Vector2 prev{};
            bool prevValid = false;
            for (int i = 0; i <= kSegments; ++i)
            {
                const float t = (2.0f * NS::Math::kPi * static_cast<float>(i)) / static_cast<float>(kSegments);
                NS::Math::Vector2 screen{};
                const bool ok = ProjectToScreen(RingPoint(axis, center, t, rotation, radius), vp, viewport, screen);
                if (ok && prevValid)
                {
                    const float d = DistancePointToSegment(mouse2d, prev, screen);
                    if (d < best)
                        best = d;
                }
                prev = screen;
                prevValid = ok;
            }
            return best;
        }
    } // namespace

    void GizmoEditor::SetSelectableObjects(std::span<NS::Scene::GameObject* const> objects,
                                           std::span<const NS::Math::Vector3> localHalfExtents,
                                           std::span<const std::uint8_t> pickable) noexcept
    {
        m_objects = objects;
        m_halfExtents = localHalfExtents;
        m_pickable = pickable;
    }

    void GizmoEditor::Tick(const NS::Math::Matrix& viewProjection, NS::Math::Size2D viewport) noexcept
    {
        if (!m_active || m_input == nullptr)
            return;

        NS::Platform::Keyboard& kb = m_input->Keyboard();
        NS::Platform::Mouse& mouse = m_input->Mouse();

        const bool imguiWantsKeyboard = (m_imgui != nullptr) && m_imgui->WantCaptureKeyboard();
        const bool imguiWantsMouse = (m_imgui != nullptr) && m_imgui->WantCaptureMouse();

        // ツール切替。 ImGui がキー入力を握っている間と、 開始ツールで確定させるドラッグ中は触らない
        if (!imguiWantsKeyboard && !m_dragging)
        {
            if (kb.IsPressed(NS::Platform::Key::Q))
                OnToolKey(NS::Platform::Key::Q);
            if (kb.IsPressed(NS::Platform::Key::W))
                OnToolKey(NS::Platform::Key::W);
            if (kb.IsPressed(NS::Platform::Key::E))
                OnToolKey(NS::Platform::Key::E);
            if (kb.IsPressed(NS::Platform::Key::R))
                OnToolKey(NS::Platform::Key::R);
            // X で Move/Rotate の座標系を Local↔World 切替。 Scale は常に Local 固定
            if (kb.IsPressed(NS::Platform::Key::X))
                ToggleSpace();
        }

        const NS::Math::Vector2 mouse2d{static_cast<float>(mouse.GetX()), static_cast<float>(mouse.GetY())};
        const bool snap = kb.IsHeld(NS::Platform::Key::Ctrl);

        // ドラッグ中は、 開始時に対象を握ったら ImGui に乗っても離すまで継続する
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
                // 離した瞬間にドラッグ終了。 undo 確定は controller が drag 終了を検出して行う
                m_dragging = false;
                m_dragAxis = GizmoAxis::None;
            }
            return;
        }

        // 新規プレス。 ImGui ウィンドウ上では開始しない
        if (!mouse.IsPressed(NS::Platform::MouseButton::Left) || imguiWantsMouse)
            return;

        // 変形ツールで選択中なら、 ハンドル優先でまず掴めるか調べる
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

        // ハンドル外をクリック → オブジェクトを選び直す。 無ヒットは選択解除
        const NS::Math::Ray ray = NS::Editor::ScreenToWorldRay(viewProjection, viewport, mouse.GetX(), mouse.GetY());
        std::vector<NS::Math::Matrix> worldMatrices;
        worldMatrices.reserve(m_objects.size());
        for (const NS::Scene::GameObject* obj : m_objects)
            worldMatrices.push_back(obj->Root().WorldMatrix());

        // まず見える実体だけで拾う。 見えないマーカー (メッシュを持たないカメラ等) が、 重なった
        // ブロックの手前でクリックを奪わないよう、 可視ヒットが無いときだけ全体を対象にもう一度撃つ
        int hit = PickNearestObb(ray, worldMatrices, m_halfExtents, m_pickable);
        if (hit < 0)
            hit = PickNearestObb(ray, worldMatrices, m_halfExtents);
        m_selected = (hit >= 0) ? &m_objects[static_cast<std::size_t>(hit)]->Root() : nullptr;
    }

    void GizmoEditor::Render(const NS::Math::Matrix& viewProjection, NS::Math::Size2D viewport) noexcept
    {
#if NS_EDITOR_ENABLED
        if (!m_active || m_selected == nullptr)
            return;
        if (viewport.width <= 0 || viewport.height <= 0)
            return;

        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        if (dl == nullptr)
            return;

        const NS::Math::Vector3 origin = m_selected->Position();
        // ハンドル/リングを置く座標系。 Move/Rotate は space に従い、 Scale は常に local
        const NS::Math::Quaternion rotation = EffectiveAxisOrientation(m_tool, m_space, m_selected->Rotation());
        NS::Math::Vector2 origin2d{};
        if (!ProjectToScreen(origin, viewProjection, viewport, origin2d))
            return;

        const ImVec2 originPx{origin2d.x, origin2d.y};

        // Select は変形ハンドルを持たないので原点マーカーだけ出す
        if (m_tool == GizmoTool::Select)
        {
            dl->AddCircleFilled(originPx, 5.0f, IM_COL32(255, 220, 60, 255));
            return;
        }

        // 現在の座標系を原点脇に出す。 Scale は常に Local 固定なので Local 表示
        const bool worldEffective = (m_tool != GizmoTool::Scale) && (m_space == GizmoSpace::World);
        dl->AddText(ImVec2{originPx.x + 12.0f, originPx.y + 8.0f},
                    IM_COL32(235, 235, 235, 255),
                    worldEffective ? "World" : "Local");

        const ImU32 axisColors[3] = {
            IM_COL32(230, 70, 70, 255),
            IM_COL32(70, 220, 90, 255),
            IM_COL32(90, 140, 255, 255),
        };
        const GizmoAxis axes[3] = {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};

        // 遠距離でもハンドルの見かけ寸法を保つ world 長。 描画とピックで同じ値を使い両者を一致させる
        const float handleLength = HandleWorldLength(origin, viewProjection);

        // 回転は軸に直交するリングで表す。 picking の DistanceToRing と同じ平面/半径で見た目と掴みを一致させる
        if (m_tool == GizmoTool::Rotate)
        {
            constexpr int kSegments = 48;
            for (int a = 0; a < 3; ++a)
            {
                ImVec2 prev{};
                bool prevValid = false;
                for (int i = 0; i <= kSegments; ++i)
                {
                    const float t = (2.0f * NS::Math::kPi * static_cast<float>(i)) / static_cast<float>(kSegments);
                    NS::Math::Vector2 screen{};
                    const bool ok = ProjectToScreen(
                        RingPoint(axes[a], origin, t, rotation, handleLength), viewProjection, viewport, screen);
                    const ImVec2 cur{screen.x, screen.y};
                    if (ok && prevValid)
                        dl->AddLine(prev, cur, axisColors[a], 2.0f);
                    prev = cur;
                    prevValid = ok;
                }
            }
            return;
        }

        // 移動 / スケールは 3 軸線。 端点は移動=丸、 スケール=箱で示す
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
                continue;

            const ImVec2 endPx{end2d.x, end2d.y};
            dl->AddLine(originPx, endPx, axisColors[i], 2.5f);

            if (m_tool == GizmoTool::Scale)
            {
                constexpr float kBoxHalf = 4.0f;
                dl->AddRectFilled(ImVec2{endPx.x - kBoxHalf, endPx.y - kBoxHalf},
                                  ImVec2{endPx.x + kBoxHalf, endPx.y + kBoxHalf},
                                  axisColors[i]);
            }
            else
            {
                // 移動は軸の先端に三角の矢じりを描く。 screen 投影した軸方向に沿って外向きに尖らせる
                const float dx = endPx.x - originPx.x;
                const float dy = endPx.y - originPx.y;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len > 1.0e-3f)
                {
                    constexpr float kHeadLength = 13.0f;
                    constexpr float kHeadHalfWidth = 5.0f;
                    const float ux = dx / len;
                    const float uy = dy / len;
                    // 軸方向に直交する単位ベクトル -uy, ux を半幅分ふって底辺 2 点を作る
                    const ImVec2 baseLeft{endPx.x - ux * kHeadLength - uy * kHeadHalfWidth,
                                          endPx.y - uy * kHeadLength + ux * kHeadHalfWidth};
                    const ImVec2 baseRight{endPx.x - ux * kHeadLength + uy * kHeadHalfWidth,
                                           endPx.y - uy * kHeadLength - ux * kHeadHalfWidth};
                    dl->AddTriangleFilled(endPx, baseLeft, baseRight, axisColors[i]);
                }
                else
                {
                    // 軸が真正面を向いて screen 上で縮退した時は丸でフォールバック
                    dl->AddCircleFilled(endPx, 5.0f, axisColors[i]);
                }
            }
        }

        // スケールの中心 Uniform ハンドルは白い箱で示す
        if (m_tool == GizmoTool::Scale)
        {
            constexpr float kCenterHalf = 5.0f;
            dl->AddRectFilled(ImVec2{originPx.x - kCenterHalf, originPx.y - kCenterHalf},
                              ImVec2{originPx.x + kCenterHalf, originPx.y + kCenterHalf},
                              IM_COL32(235, 235, 235, 255));
        }
#else
        (void)viewProjection;
        (void)viewport;
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
        // size 不一致は短い方まで。 アフィン逆変換は ray パラメータ t を保つので、 各 box の
        // ローカル交差 t をそのままワールド ray の t として object 間で大小比較できる
        const std::size_t count = std::min(worldMatrices.size(), localHalfExtents.size());
        int best = -1;
        float bestT = 0.0f;
        for (std::size_t i = 0; i < count; ++i)
        {
            // mask を渡した場合は 0 の要素を対象外にする。 空 mask は全対象
            if (!pickMask.empty() && (i >= pickMask.size() || pickMask[i] == 0))
                continue;
            const NS::Math::Matrix inv = worldMatrices[i].Invert();
            const NS::Math::Vector3 localOrigin = NS::Math::Vector3::Transform(ray.position, inv);
            // 方向は w=0 の線形部のみ変換し、 正規化しない。 正規化すると t がローカル長さに巻き込まれ比較が壊れる
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
            return startPos;

        const NS::Math::Vector3 a = OrientedAxis(axis, rotation);

        // viewDir を正規化して軸との平行度を測る。 ほぼ平行ならドラッグ平面が薄く交点が暴れるので何もしない
        NS::Math::Vector3 viewDir = rayNow.direction;
        viewDir.Normalize();
        if (std::fabs(a.Dot(viewDir)) >= kAxisViewParallelEpsilon)
            return startPos;

        // 軸 a を含み、 視線に最も正対する平面の法線 = a × (viewDir × a)
        // viewDir のうち a に直交する成分を向き、 a 周りで最も視線を受ける向き
        NS::Math::Vector3 normal = a.Cross(viewDir.Cross(a));
        if (normal.LengthSquared() < kPlaneParallelEpsilon)
            return startPos;
        normal.Normalize();

        NS::Math::Vector3 p0{};
        NS::Math::Vector3 p1{};
        if (!IntersectRayWithPlane(rayStart, startPos, normal, p0) ||
            !IntersectRayWithPlane(rayNow, startPos, normal, p1))
            return startPos;

        float delta = (p1 - p0).Dot(a);
        // local 軸に沿う移動量を刻みに丸める。 world 成分でなく軸方向の距離をスナップする
        if (snap)
            delta = SnapTo(delta, kMoveSnapStep);
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
        if (n.LengthSquared() < 0.5f) // None/Uniform は零ベクトルなので X/Y/Z 以外は回さない
            return 0.0f;

        const NS::Math::Ray rayStart = NS::Editor::ScreenToWorldRay(
            viewProjection, viewport, static_cast<int>(screenStart.x), static_cast<int>(screenStart.y));
        const NS::Math::Ray rayNow = NS::Editor::ScreenToWorldRay(
            viewProjection, viewport, static_cast<int>(screenEnd.x), static_cast<int>(screenEnd.y));

        // origin を通り法線 n を持つ軸直交平面との交点で、 掴んだ点を world 座標に復元する
        NS::Math::Vector3 hitStart{};
        NS::Math::Vector3 hitNow{};
        if (!IntersectRayWithPlane(rayStart, origin, n, hitStart) || !IntersectRayWithPlane(rayNow, origin, n, hitNow))
            return 0.0f;

        const NS::Math::Vector3 v0 = hitStart - origin;
        const NS::Math::Vector3 v1 = hitNow - origin;
        if (v0.LengthSquared() < kRingGrabRadiusEpsilonSq || v1.LengthSquared() < kRingGrabRadiusEpsilonSq)
            return 0.0f;

        // 軸まわりの符号付き角。 cross の軸成分が回転の向きを与えるのでカメラの視点側に依存しない
        const float sinComponent = v0.Cross(v1).Dot(n);
        const float cosComponent = v0.Dot(v1);
        return std::atan2(sinComponent, cosComponent);
    }

    NS::Math::Quaternion GizmoEditor::ComputeAxisRotate(
        const NS::Math::Quaternion& startRot, GizmoAxis axis, float angleRad, bool snap, bool worldSpace) noexcept
    {
        if (axis != GizmoAxis::X && axis != GizmoAxis::Y && axis != GizmoAxis::Z)
            return startRot;

        const float angle = snap ? SnapTo(angleRad, kRotateSnapStep) : angleRad;
        // 回転軸 n は world ならそのまま world 軸、 local なら startRot で回した選択物の local 軸。 リング描画と
        // 角度計測も同じ n を使うので見た目と一致する
        const NS::Math::Vector3 n = worldSpace ? AxisVector(axis) : OrientedAxis(axis, startRot);
        const NS::Math::Quaternion delta = NS::Math::Quaternion::CreateFromAxisAngle(n, angle);

        // q1*q2 は「q1 を先に、 続けて q2」 の合成。 startRot を効かせた後に world 軸 n 周りで delta を
        // 後がけすると見た目 n 周りの回転になる。 合成基準は常に startRot なので space に依らず正しく繋がる
        return startRot * delta;
    }

    float GizmoEditor::ScreenDragToScaleAmount(NS::Math::Vector2 axisDir2d, NS::Math::Vector2 dragPixels) noexcept
    {
        // 軸の screen 投影が縮退、 つまりカメラがその軸を真正面/真後ろから見ていると方向が定まらない
        // ので、 ドラッグ長 * X 符号でフォールバックし「右ドラッグで拡大」 を保つ
        const float axisLen = axisDir2d.Length();
        if (axisLen <= 1.0e-6f)
        {
            const float sign = (dragPixels.x < 0.0f) ? -1.0f : 1.0f;
            return dragPixels.Length() * kScaleSensitivity * sign;
        }

        const NS::Math::Vector2 axisUnit{axisDir2d.x / axisLen, axisDir2d.y / axisLen};
        return dragPixels.Dot(axisUnit) * kScaleSensitivity;
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
            result.x = SnapTo(result.x, kScaleSnapStep);
            result.y = SnapTo(result.y, kScaleSnapStep);
            result.z = SnapTo(result.z, kScaleSnapStep);
        }

        // snap が 0 に丸めたケースも含め、 ここで最小正値へ押し上げる
        result.x = (result.x < kScaleMin) ? kScaleMin : result.x;
        result.y = (result.y < kScaleMin) ? kScaleMin : result.y;
        result.z = (result.z < kScaleMin) ? kScaleMin : result.z;
        return result;
    }

    GizmoAxis GizmoEditor::ToolHandlePick(const NS::Math::Vector3& gizmoOrigin,
                                          const NS::Math::Quaternion& rotation,
                                          GizmoTool tool,
                                          NS::Math::Vector2 mouse2d,
                                          const NS::Math::Matrix& viewProjection,
                                          NS::Math::Size2D viewport) noexcept
    {
        // Select は変形ハンドルを持たないので拾わない
        if (tool == GizmoTool::Select)
            return GizmoAxis::None;
        if (viewport.width <= 0 || viewport.height <= 0)
            return GizmoAxis::None;

        NS::Math::Vector2 origin2d{};
        if (!ProjectToScreen(gizmoOrigin, viewProjection, viewport, origin2d))
            return GizmoAxis::None;

        // 回転は軸に直交するリングを掴む。 各軸リングへの screen 最短距離で最近を選び見た目のリングと一致させる
        if (tool == GizmoTool::Rotate)
        {
            const GizmoAxis ringAxes[3] = {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};
            GizmoAxis bestRing = GizmoAxis::None;
            float bestRingDistance = kPickThresholdPixels;
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

        // Scale の中心 Uniform ハンドルは軸より優先する。 全軸線は中心から放射するため中心近傍を必ず通り、
        // 単純な最近接比較だと中心を狙っても僅かに近い軸に取られる。 中心が閾値内なら軸評価前に確定させる
        if (tool == GizmoTool::Scale)
        {
            const float dx = mouse2d.x - origin2d.x;
            const float dy = mouse2d.y - origin2d.y;
            if (std::sqrt(dx * dx + dy * dy) < kPickThresholdPixels)
                return GizmoAxis::Uniform;
        }

        const float handleLength = HandleWorldLength(gizmoOrigin, viewProjection);
        const GizmoAxis axisEnum[3] = {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};

        GizmoAxis best = GizmoAxis::None;
        float bestDistance = kPickThresholdPixels;
        for (int i = 0; i < 3; ++i)
        {
            const NS::Math::Vector3 dir = OrientedAxis(axisEnum[i], rotation);
            const NS::Math::Vector3 endWorld{
                gizmoOrigin.x + dir.x * handleLength,
                gizmoOrigin.y + dir.y * handleLength,
                gizmoOrigin.z + dir.z * handleLength,
            };
            NS::Math::Vector2 end2d{};
            // 端点が背面に回った軸はその軸だけスキップする
            if (!ProjectToScreen(endWorld, viewProjection, viewport, end2d))
                continue;

            const float dist = DistancePointToSegment(mouse2d, origin2d, end2d);
            if (dist < bestDistance)
            {
                bestDistance = dist;
                best = axisEnum[i];
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
            return;

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
