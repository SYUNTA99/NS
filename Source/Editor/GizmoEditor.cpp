#include "Editor/GizmoEditor.h"

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
        // 移動スナップの刻み (ワールド単位)
        constexpr float kMoveSnapStep = 0.5f;

        // 回転スナップの刻み (15 度 = π/12 rad)
        constexpr float kRotateSnapStep = NS::Math::kPi / 12.0f;

        // スケールスナップの刻み
        constexpr float kScaleSnapStep = 0.25f;

        // ドラッグ 1px あたりのスケール変化量
        constexpr float kScaleSensitivity = 0.01f;

        // 0 以下に潰れると mesh が反転/消失するので下限を張る
        constexpr float kScaleMin = 0.01f;

        // ハンドル軸の長さ (world)。 origin から各軸方向にこの距離だけ伸ばした端点を picking に使う
        constexpr float kHandleLength = 1.0f;

        // screen 上のヒット許容半径 (px)。 これ未満の最近接軸を採用する
        constexpr float kPickThresholdPixels = 12.0f;

        // 軸と視線がこれ以上そろうと、 軸を含む平面が薄くなって交点が暴れるので no-op にする
        constexpr float kAxisViewParallelEpsilon = 0.999f;

        // ray と平面の交差判定で、 分母 (rayDir・n) がこの値未満なら平行とみなし交差不能
        constexpr float kPlaneParallelEpsilon = 1e-6f;

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

        // startPos を通り axis を含む平面 (法線 planeNormal) と ray の交点。 交差不能なら false
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

        // ray と中心原点 AABB のスラブ判定 (返す t は ray パラメータで方向のスケールを保つので
        // object 間でそのまま大小比較できる)。 SimpleMath の Ray::Intersects は方向が単位ベクトル
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

        // world 点を screen へ投影する。 clip.w<=0 (カメラ背面) は false。 式は RenderCursorPreview と同一
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

        // 点 p から線分 a-b への最短距離 (px)。 線分が縮退 (a==b) なら点 a への距離
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

        // ドラッグ (screenStart→screenEnd) を現ツール / 軸の新 PRS へ変換する
        // Tick のライブプレビューと ApplyDragForTest が同じ算出を共有する
        [[nodiscard]] TransformState ComputeDragResult(const TransformState& before,
                                                       GizmoTool tool,
                                                       GizmoAxis axis,
                                                       const NS::Math::Matrix& viewProjection,
                                                       NS::Math::Size2D viewport,
                                                       NS::Math::Vector2 screenStart,
                                                       NS::Math::Vector2 screenEnd,
                                                       bool snap) noexcept
        {
            TransformState after = before;
            switch (tool)
            {
            case GizmoTool::Move:
            {
                const NS::Math::Ray rayStart = NS::Scene::EditorGridMath::ScreenToWorldRay(
                    viewProjection, viewport, static_cast<int>(screenStart.x), static_cast<int>(screenStart.y));
                const NS::Math::Ray rayNow = NS::Scene::EditorGridMath::ScreenToWorldRay(
                    viewProjection, viewport, static_cast<int>(screenEnd.x), static_cast<int>(screenEnd.y));
                after.position = GizmoEditor::ComputeAxisMove(before.position, axis, rayStart, rayNow, snap);
                break;
            }
            case GizmoTool::Rotate:
            {
                NS::Math::Vector2 origin2d{};
                if (ProjectToScreen(before.position, viewProjection, viewport, origin2d))
                {
                    const float angle = GizmoEditor::ScreenDragToAngle(origin2d, screenStart, screenEnd);
                    after.rotation = GizmoEditor::ComputeAxisRotate(before.rotation, axis, angle, snap);
                }
                break;
            }
            case GizmoTool::Scale:
            {
                NS::Math::Vector2 axisDir2d{};
                if (axis != GizmoAxis::Uniform)
                {
                    NS::Math::Vector2 origin2d{};
                    NS::Math::Vector2 end2d{};
                    const NS::Math::Vector3 axisEnd = before.position + AxisVector(axis) * kHandleLength;
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

        // 軸 axis 周りの半径 kHandleLength のリング上の点 (axis に直交する平面内、 角度 t)
        [[nodiscard]] NS::Math::Vector3 RingPoint(GizmoAxis axis, const NS::Math::Vector3& center, float t) noexcept
        {
            const float c = std::cos(t) * kHandleLength;
            const float s = std::sin(t) * kHandleLength;
            switch (axis)
            {
            case GizmoAxis::X:
                return {center.x, center.y + c, center.z + s};
            case GizmoAxis::Y:
                return {center.x + c, center.y, center.z + s};
            case GizmoAxis::Z:
                return {center.x + c, center.y + s, center.z};
            default:
                return center;
            }
        }

        // リングを screen 折れ線に投影し mouse2d との最短距離 (px) を返す。 背面に回った区間は除外する
        [[nodiscard]] float DistanceToRing(GizmoAxis axis,
                                           const NS::Math::Vector3& center,
                                           const NS::Math::Matrix& vp,
                                           NS::Math::Size2D viewport,
                                           NS::Math::Vector2 mouse2d) noexcept
        {
            constexpr int kSegments = 32;
            float best = 1.0e30f;
            NS::Math::Vector2 prev{};
            bool prevValid = false;
            for (int i = 0; i <= kSegments; ++i)
            {
                const float t = (2.0f * NS::Math::kPi * static_cast<float>(i)) / static_cast<float>(kSegments);
                NS::Math::Vector2 screen{};
                const bool ok = ProjectToScreen(RingPoint(axis, center, t), vp, viewport, screen);
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
                                           std::span<const NS::Math::Vector3> localHalfExtents) noexcept
    {
        m_objects = objects;
        m_halfExtents = localHalfExtents;
    }

    void GizmoEditor::Tick(const NS::Math::Matrix& viewProjection, NS::Math::Size2D viewport) noexcept
    {
        if (!m_active || m_input == nullptr)
            return;

        NS::Platform::Keyboard& kb = m_input->Keyboard();
        NS::Platform::Mouse& mouse = m_input->Mouse();

        const bool imguiWantsKeyboard = (m_imgui != nullptr) && m_imgui->WantCaptureKeyboard();
        const bool imguiWantsMouse = (m_imgui != nullptr) && m_imgui->WantCaptureMouse();

        // ツール切替。 ImGui がキー入力を握っている間と、 ドラッグ中 (開始ツールで確定させる) は触らない
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
        }

        const NS::Math::Vector2 mouse2d{static_cast<float>(mouse.GetX()), static_cast<float>(mouse.GetY())};
        const bool snap = kb.IsHeld(NS::Platform::Key::Ctrl);

        // ドラッグ中は、 開始時に対象を握ったら ImGui に乗っても離すまで継続する
        if (m_dragging)
        {
            if (m_selected != nullptr && mouse.IsHeld(NS::Platform::MouseButton::Left))
            {
                const TransformState preview = ComputeDragResult(
                    m_dragBefore, m_tool, m_dragAxis, viewProjection, viewport, m_dragStartScreen, mouse2d, snap);
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

        // 変形ツールで選択中なら、 まずハンドルを掴めるか調べる (ハンドル優先)
        if (m_selected != nullptr && m_tool != GizmoTool::Select)
        {
            const GizmoAxis axis = ToolHandlePick(m_selected->Position(), m_tool, mouse2d, viewProjection, viewport);
            if (axis != GizmoAxis::None)
            {
                m_dragging = true;
                m_dragAxis = axis;
                m_dragStartScreen = mouse2d;
                m_dragBefore = TransformState{m_selected->Position(), m_selected->Rotation(), m_selected->Scale()};
                return;
            }
        }

        // ハンドル外をクリック → オブジェクトを選び直す (無ヒットは選択解除)
        const NS::Math::Ray ray =
            NS::Scene::EditorGridMath::ScreenToWorldRay(viewProjection, viewport, mouse.GetX(), mouse.GetY());
        std::vector<NS::Math::Matrix> worldMatrices;
        worldMatrices.reserve(m_objects.size());
        for (const NS::Scene::GameObject* obj : m_objects)
            worldMatrices.push_back(obj->Root().WorldMatrix());

        const int hit = PickNearestObb(ray, worldMatrices, m_halfExtents);
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

        const ImU32 axisColors[3] = {
            IM_COL32(230, 70, 70, 255),
            IM_COL32(70, 220, 90, 255),
            IM_COL32(90, 140, 255, 255),
        };
        const GizmoAxis axes[3] = {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};

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
                    const bool ok = ProjectToScreen(RingPoint(axes[a], origin, t), viewProjection, viewport, screen);
                    const ImVec2 cur{screen.x, screen.y};
                    if (ok && prevValid)
                        dl->AddLine(prev, cur, axisColors[a], 2.0f);
                    prev = cur;
                    prevValid = ok;
                }
            }
            return;
        }

        // 移動 / スケールは 3 軸線。 端点は移動=丸、 スケール=箱 (Maya のスケールハンドル表記)
        for (int i = 0; i < 3; ++i)
        {
            const NS::Math::Vector3 dir = AxisVector(axes[i]);
            const NS::Math::Vector3 endWorld{
                origin.x + dir.x * kHandleLength,
                origin.y + dir.y * kHandleLength,
                origin.z + dir.z * kHandleLength,
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
                // 移動は軸の先端に矢じり (三角) を描く。 screen 投影した軸方向に沿って外向きに尖らせる
                const float dx = endPx.x - originPx.x;
                const float dy = endPx.y - originPx.y;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len > 1.0e-3f)
                {
                    constexpr float kHeadLength = 13.0f;
                    constexpr float kHeadHalfWidth = 5.0f;
                    const float ux = dx / len;
                    const float uy = dy / len;
                    // 軸方向に直交する単位ベクトル (-uy, ux) を半幅分ふって底辺 2 点を作る
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

        // スケールの中心 (Uniform) ハンドルは白い箱で示す
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
                                    std::span<const NS::Math::Vector3> localHalfExtents) noexcept
    {
        // size 不一致は短い方まで。 アフィン逆変換は ray パラメータ t を保つので、 各 box の
        // ローカル交差 t をそのままワールド ray の t として object 間で大小比較できる
        const std::size_t count = std::min(worldMatrices.size(), localHalfExtents.size());
        int best = -1;
        float bestT = 0.0f;
        for (std::size_t i = 0; i < count; ++i)
        {
            const NS::Math::Matrix inv = worldMatrices[i].Invert();
            const NS::Math::Vector3 localOrigin = NS::Math::Vector3::Transform(ray.position, inv);
            // 方向は w=0 の線形部のみ変換し、 正規化しない (正規化すると t がローカル長さに巻き込まれ比較が壊れる)
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
                                                   const NS::Math::Ray& rayStart,
                                                   const NS::Math::Ray& rayNow,
                                                   bool snap) noexcept
    {
        if (axis != GizmoAxis::X && axis != GizmoAxis::Y && axis != GizmoAxis::Z)
            return startPos;

        const NS::Math::Vector3 a = AxisVector(axis);

        // viewDir を正規化して軸との平行度を測る。 ほぼ平行ならドラッグ平面が薄く交点が暴れるので no-op
        NS::Math::Vector3 viewDir = rayNow.direction;
        viewDir.Normalize();
        if (std::fabs(a.Dot(viewDir)) >= kAxisViewParallelEpsilon)
            return startPos;

        // 軸 a を含み、 視線に最も正対する平面の法線 = a × (viewDir × a)
        // (viewDir のうち a に直交する成分を向く。 a 周りで最も視線を受ける向き)
        NS::Math::Vector3 normal = a.Cross(viewDir.Cross(a));
        if (normal.LengthSquared() < kPlaneParallelEpsilon)
            return startPos;
        normal.Normalize();

        NS::Math::Vector3 p0{};
        NS::Math::Vector3 p1{};
        if (!IntersectRayWithPlane(rayStart, startPos, normal, p0) ||
            !IntersectRayWithPlane(rayNow, startPos, normal, p1))
            return startPos;

        const float delta = (p1 - p0).Dot(a);
        NS::Math::Vector3 newPos = startPos + a * delta;

        if (snap)
        {
            // 動かした軸成分だけをグリッドにスナップする (他 2 軸は startPos のまま)
            if (axis == GizmoAxis::X)
                newPos.x = SnapTo(newPos.x, kMoveSnapStep);
            else if (axis == GizmoAxis::Y)
                newPos.y = SnapTo(newPos.y, kMoveSnapStep);
            else
                newPos.z = SnapTo(newPos.z, kMoveSnapStep);
        }
        return newPos;
    }

    float GizmoEditor::ScreenDragToAngle(NS::Math::Vector2 origin2d,
                                         NS::Math::Vector2 start2d,
                                         NS::Math::Vector2 now2d) noexcept
    {
        // origin から start / now へ伸びるベクトルの符号付きなす角を atan2(cross, dot) で取る
        const NS::Math::Vector2 v0 = start2d - origin2d;
        const NS::Math::Vector2 v1 = now2d - origin2d;
        const float cross = v0.x * v1.y - v0.y * v1.x;
        const float dot = v0.x * v1.x + v0.y * v1.y;
        return std::atan2(cross, dot);
    }

    NS::Math::Quaternion GizmoEditor::ComputeAxisRotate(const NS::Math::Quaternion& startRot,
                                                        GizmoAxis axis,
                                                        float angleRad,
                                                        bool snap) noexcept
    {
        if (axis != GizmoAxis::X && axis != GizmoAxis::Y && axis != GizmoAxis::Z)
            return startRot;

        const float angle = snap ? SnapTo(angleRad, kRotateSnapStep) : angleRad;
        const NS::Math::Quaternion delta = NS::Math::Quaternion::CreateFromAxisAngle(AxisVector(axis), angle);

        // delta はワールド軸の回転。 q1*q2 は「q1 を先に、 続けて q2」 の合成なので、
        // startRot を先に効かせてから delta を「ワールド座標で」 後がけする順序が startRot * delta になる
        // (delta * startRot にすると delta が startRot で回されローカル軸回転に化ける)
        return startRot * delta;
    }

    float GizmoEditor::ScreenDragToScaleAmount(NS::Math::Vector2 axisDir2d, NS::Math::Vector2 dragPixels) noexcept
    {
        // 軸の screen 投影が縮退 (カメラがその軸を真正面/真後ろから見ている) すると方向が定まらない
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

        // 回転は軸に直交するリングを掴む。 各軸リングへの screen 最短距離で最近を選ぶ (見た目のリングと一致)
        if (tool == GizmoTool::Rotate)
        {
            const GizmoAxis ringAxes[3] = {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};
            GizmoAxis bestRing = GizmoAxis::None;
            float bestRingDistance = kPickThresholdPixels;
            for (int i = 0; i < 3; ++i)
            {
                const float d = DistanceToRing(ringAxes[i], gizmoOrigin, viewProjection, viewport, mouse2d);
                if (d < bestRingDistance)
                {
                    bestRingDistance = d;
                    bestRing = ringAxes[i];
                }
            }
            return bestRing;
        }

        // Scale の中心 (Uniform) ハンドルは軸より優先する。 全軸線は中心から放射するため中心近傍を必ず通り、
        // 単純な最近接比較だと中心を狙っても僅かに近い軸に取られる。 中心が閾値内なら軸評価前に確定させる
        if (tool == GizmoTool::Scale)
        {
            const float dx = mouse2d.x - origin2d.x;
            const float dy = mouse2d.y - origin2d.y;
            if (std::sqrt(dx * dx + dy * dy) < kPickThresholdPixels)
                return GizmoAxis::Uniform;
        }

        const NS::Math::Vector3 worldAxes[3] = {
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
        };
        const GizmoAxis axisEnum[3] = {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};

        GizmoAxis best = GizmoAxis::None;
        float bestDistance = kPickThresholdPixels;
        for (int i = 0; i < 3; ++i)
        {
            const NS::Math::Vector3 endWorld{
                gizmoOrigin.x + worldAxes[i].x * kHandleLength,
                gizmoOrigin.y + worldAxes[i].y * kHandleLength,
                gizmoOrigin.z + worldAxes[i].z * kHandleLength,
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
            ComputeDragResult(before, m_tool, axis, viewProjection, viewport, screenStart, screenEnd, false);
        ApplyState(*m_selected, after);
    }

    void GizmoEditor::OnToolKey(NS::Platform::Key key) noexcept
    {
        m_tool = ToolForKey(m_tool, key);
    }

} // namespace NS::Editor
