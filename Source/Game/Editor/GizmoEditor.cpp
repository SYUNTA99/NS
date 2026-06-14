#include "Game/Editor/GizmoEditor.h"

#include <algorithm>
#include <cmath>

namespace NS::Game::Editor
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
    } // namespace

    void GizmoEditor::SetSelectableObjects(std::span<NS::Scene::GameObject* const> objects,
                                           std::span<const NS::Math::Vector3> localHalfExtents) noexcept
    {
        m_objects = objects;
        m_halfExtents = localHalfExtents;
    }

    void GizmoEditor::Tick(const NS::Math::Matrix&, NS::Math::Size2D) noexcept {}

    void GizmoEditor::Render(const NS::Math::Matrix&, NS::Math::Size2D) noexcept {}

    bool GizmoEditor::Undo() noexcept
    {
        return false;
    }

    bool GizmoEditor::Redo() noexcept
    {
        return false;
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
            const NS::Math::Ray localRay{localOrigin, localDir};
            const NS::Math::AABB localBox(NS::Math::Vector3{0.0f, 0.0f, 0.0f}, localHalfExtents[i]);
            float t = 0.0f;
            if (localRay.Intersects(localBox, t) && (best < 0 || t < bestT))
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

    void GizmoEditor::ApplyDragForTest(
        const NS::Math::Matrix&, NS::Math::Size2D, GizmoAxis, NS::Math::Vector2, NS::Math::Vector2) noexcept
    {}

    void GizmoEditor::OnToolKey(NS::Platform::Key key) noexcept
    {
        m_tool = ToolForKey(m_tool, key);
    }
} // namespace NS::Game::Editor
