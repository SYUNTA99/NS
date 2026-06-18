#include "Framework/Physics/SweptOBB.h"

#include <algorithm>
#include <cmath>

namespace
{
    using NS::Math::Vector3;
    using NS::Physics::OBB;

    [[nodiscard]] float Dot(const Vector3& a, const Vector3& b) noexcept
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    /// world ベクトルを OBB local 軸へ射影する (回転のみ、 平行移動なし)
    [[nodiscard]] Vector3 ToLocalDir(const Vector3& v, const OBB& obb) noexcept
    {
        return Vector3{Dot(v, obb.axisX), Dot(v, obb.axisY), Dot(v, obb.axisZ)};
    }

    /// sphere (start, radius) が motion だけ動いた時に OBB と最初に当たる TOI を返す
    /// 始点と変位を OBB local 軸へ移し、 原点中心の膨張 box への slab test へ帰着する
    [[nodiscard]] bool SweptSphereVsObb(const Vector3& start,
                                        const Vector3& motion,
                                        const OBB& obb,
                                        float radius,
                                        float& outToi,
                                        Vector3& outNormal) noexcept
    {
        outToi = 1.0f;
        outNormal = Vector3{0.0f, 0.0f, 0.0f};

        const Vector3 p = ToLocalDir(start - obb.center, obb);
        const Vector3 m = ToLocalDir(motion, obb);

        const float ex = obb.halfExtents.x + radius;
        const float ey = obb.halfExtents.y + radius;
        const float ez = obb.halfExtents.z + radius;

        // 既に膨張 box 内部 -> toi=0、 最浅軸の押し戻し方向を local 法線にして world へ戻す
        if (std::abs(p.x) < ex && std::abs(p.y) < ey && std::abs(p.z) < ez)
        {
            outToi = 0.0f;
            const float px = ex - std::abs(p.x);
            const float py = ey - std::abs(p.y);
            const float pz = ez - std::abs(p.z);
            Vector3 localN{};
            if (px <= py && px <= pz)
                localN = {p.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f};
            else if (py <= pz)
                localN = {0.0f, p.y >= 0.0f ? 1.0f : -1.0f, 0.0f};
            else
                localN = {0.0f, 0.0f, p.z >= 0.0f ? 1.0f : -1.0f};
            outNormal = obb.axisX * localN.x + obb.axisY * localN.y + obb.axisZ * localN.z;
            return true;
        }

        const float boxMin[3] = {-ex, -ey, -ez};
        const float boxMax[3] = {ex, ey, ez};
        const float ps[3] = {p.x, p.y, p.z};
        const float ms[3] = {m.x, m.y, m.z};

        float tNear = 0.0f;
        float tFar = 1.0f;
        int nearAxis = -1;
        float nearSign = 0.0f;

        for (int axis = 0; axis < 3; ++axis)
        {
            if (std::abs(ms[axis]) < 1e-9f)
            {
                if (ps[axis] < boxMin[axis] || ps[axis] > boxMax[axis])
                    return false;
                continue;
            }
            const float invD = 1.0f / ms[axis];
            float t1 = (boxMin[axis] - ps[axis]) * invD;
            float t2 = (boxMax[axis] - ps[axis]) * invD;
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
                return false;
        }

        if (nearAxis < 0 || tNear < 0.0f || tNear > 1.0f)
            return false;

        outToi = tNear;
        Vector3 localN{0.0f, 0.0f, 0.0f};
        if (nearAxis == 0)
            localN.x = nearSign;
        else if (nearAxis == 1)
            localN.y = nearSign;
        else
            localN.z = nearSign;
        outNormal = obb.axisX * localN.x + obb.axisY * localN.y + obb.axisZ * localN.z;
        return true;
    }
} // namespace

namespace NS::Physics
{
    OBB MakeObb(const NS::Math::Vector3& center,
                const NS::Math::Quaternion& rotation,
                const NS::Math::Vector3& halfExtents) noexcept
    {
        OBB obb;
        obb.center = center;
        obb.axisX = NS::Math::Vector3::Transform(NS::Math::Vector3::UnitX, rotation);
        obb.axisY = NS::Math::Vector3::Transform(NS::Math::Vector3::UnitY, rotation);
        obb.axisZ = NS::Math::Vector3::Transform(NS::Math::Vector3::UnitZ, rotation);
        obb.halfExtents = NS::Math::Vector3{std::abs(halfExtents.x), std::abs(halfExtents.y), std::abs(halfExtents.z)};
        return obb;
    }

    bool SweptCapsuleVsOBB(const Capsule& capsule,
                           const NS::Math::Vector3& motion,
                           const OBB& obb,
                           float& outToi,
                           NS::Math::Vector3& outNormal) noexcept
    {
        outToi = 1.0f;
        outNormal = NS::Math::Vector3{0.0f, 0.0f, 0.0f};

        // Capsule 型は unit axis を強制しないため正規化する (非単位入力で端点位置が歪む防止)
        NS::Math::Vector3 axis = capsule.axis;
        const float axisLenSq = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
        if (axisLenSq > 1e-12f)
        {
            const float invLen = 1.0f / std::sqrt(axisLenSq);
            axis.x *= invLen;
            axis.y *= invLen;
            axis.z *= invLen;
        }
        else
        {
            axis = NS::Math::Vector3{0.0f, 1.0f, 0.0f};
        }

        const NS::Math::Vector3 top = capsule.center + axis * capsule.halfHeight;
        const NS::Math::Vector3 bottom = capsule.center - axis * capsule.halfHeight;

        float toiTop = 1.0f;
        float toiBottom = 1.0f;
        NS::Math::Vector3 normalTop{};
        NS::Math::Vector3 normalBottom{};
        const bool hitTop = SweptSphereVsObb(top, motion, obb, capsule.radius, toiTop, normalTop);
        const bool hitBottom = SweptSphereVsObb(bottom, motion, obb, capsule.radius, toiBottom, normalBottom);

        if (!hitTop && !hitBottom)
            return false;

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
