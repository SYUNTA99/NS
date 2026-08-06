#include "Runtime/Physics/SweptAABB.h"

#include <algorithm>
#include <cmath>

namespace
{
    using NS::Math::AABB;
    using NS::Math::Vector3;
    using NS::Physics::Capsule;

    /// Capsule の片端である sphere を motion だけ swept した時に AABB と最初に当たる TOI を返す
    /// AABB を radius で膨張 → 線分 vs 膨張 AABB の slab test
    bool SweptSphereVsAABB(const Vector3& start,
                           const Vector3& motion,
                           const AABB& box,
                           float radius,
                           float& outToi,
                           Vector3& outNormal) noexcept
    {
        const Vector3 boxCenter{box.Center.x, box.Center.y, box.Center.z};
        const float ex = box.Extents.x + radius;
        const float ey = box.Extents.y + radius;
        const float ez = box.Extents.z + radius;

        const Vector3 d = start - boxCenter;

        // 既に膨張 AABB 内部にいる場合 → toi=0、最浅軸の押し戻し方向を normal にする
        if (std::abs(d.x) < ex && std::abs(d.y) < ey && std::abs(d.z) < ez)
        {
            outToi = 0.0f;
            const float px = ex - std::abs(d.x);
            const float py = ey - std::abs(d.y);
            const float pz = ez - std::abs(d.z);
            if (px <= py && px <= pz)
            {
                float nx = -1.0f;
                if (d.x >= 0.0f)
                    nx = 1.0f;
                outNormal = {nx, 0.0f, 0.0f};
            }
            else if (py <= pz)
            {
                float ny = -1.0f;
                if (d.y >= 0.0f)
                    ny = 1.0f;
                outNormal = {0.0f, ny, 0.0f};
            }
            else
            {
                float nz = -1.0f;
                if (d.z >= 0.0f)
                    nz = 1.0f;
                outNormal = {0.0f, 0.0f, nz};
            }
            return true;
        }

        // 軸並行に拡張した AABB への slab 判定
        const float boxMin[3] = {boxCenter.x - ex, boxCenter.y - ey, boxCenter.z - ez};
        const float boxMax[3] = {boxCenter.x + ex, boxCenter.y + ey, boxCenter.z + ez};
        const float p[3] = {start.x, start.y, start.z};
        const float m[3] = {motion.x, motion.y, motion.z};

        float tNear = 0.0f;
        float tFar = 1.0f;
        int nearAxis = -1;
        float nearSign = 0.0f;

        for (int axis = 0; axis < 3; ++axis)
        {
            if (std::abs(m[axis]) < 1e-9f)
            {
                if (p[axis] < boxMin[axis] || p[axis] > boxMax[axis])
                {
                    outToi = 1.0f;
                    outNormal = {0.0f, 0.0f, 0.0f};
                    return false;
                }
                continue;
            }
            const float invD = 1.0f / m[axis];
            float t1 = (boxMin[axis] - p[axis]) * invD;
            float t2 = (boxMax[axis] - p[axis]) * invD;
            float sign = -1.0f;
            if (t1 > t2)
            {
                std::swap(t1, t2);
                sign = 1.0f;
            }
            if (t1 > tNear)
            {
                tNear = t1;
                nearAxis = axis;
                nearSign = sign;
            }
            if (t2 < tFar)
                tFar = t2;
            if (tNear > tFar)
            {
                outToi = 1.0f;
                outNormal = {0.0f, 0.0f, 0.0f};
                return false;
            }
        }

        if (tNear < 0.0f || tNear > 1.0f)
        {
            outToi = 1.0f;
            outNormal = {0.0f, 0.0f, 0.0f};
            return false;
        }

        outToi = tNear;
        Vector3 n{0.0f, 0.0f, 0.0f};
        if (nearAxis == 0)
            n.x = nearSign;
        else if (nearAxis == 1)
            n.y = nearSign;
        else if (nearAxis == 2)
            n.z = nearSign;
        outNormal = n;
        return true;
    }
} // namespace

namespace NS::Physics
{
    bool SweptCapsuleVsAABB(const Capsule& capsule,
                            const NS::Math::Vector3& motion,
                            const NS::Math::AABB& box,
                            float& outToi,
                            NS::Math::Vector3& outNormal) noexcept
    {
        // 上下 2 endpoint を sphere swept する近似。軸端点は共通ヘルパで求める
        const auto [top, bottom] = CapsuleEndpoints(capsule);

        float toiTop = 1.0f;
        float toiBottom = 1.0f;
        NS::Math::Vector3 normalTop{};
        NS::Math::Vector3 normalBottom{};

        const bool hitTop = SweptSphereVsAABB(top, motion, box, capsule.radius, toiTop, normalTop);
        const bool hitBottom = SweptSphereVsAABB(bottom, motion, box, capsule.radius, toiBottom, normalBottom);

        if (!hitTop && !hitBottom)
        {
            outToi = 1.0f;
            outNormal = NS::Math::Vector3{0.0f, 0.0f, 0.0f};
            return false;
        }

        if (hitTop && (!hitBottom || toiTop <= toiBottom))
        {
            outToi = toiTop;
            outNormal = normalTop;
        }
        else
        {
            outToi = toiBottom;
            outNormal = normalBottom;
        }
        return true;
    }
} // namespace NS::Physics
