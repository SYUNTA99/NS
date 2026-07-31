#include "Runtime/Physics/Capsule.h"

#include <algorithm>
#include <cmath>

namespace NS::Physics
{

    CapsuleSegment CapsuleEndpoints(const Capsule& capsule) noexcept
    {
        // Capsule 型は axis の単位長を強制しないため、非単位入力で端点が歪まないよう正規化する
        NS::Math::Vector3 axis = capsule.axis;
        const float lenSq = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
        if (lenSq > 1e-12f)
        {
            const float invLen = 1.0f / std::sqrt(lenSq);
            axis.x *= invLen;
            axis.y *= invLen;
            axis.z *= invLen;
        }
        else
        {
            axis = NS::Math::Vector3{0.0f, 1.0f, 0.0f};
        }

        CapsuleSegment seg;
        seg.top = capsule.center + axis * capsule.halfHeight;
        seg.bottom = capsule.center - axis * capsule.halfHeight;
        return seg;
    }

    bool IntersectsCapsuleAABB(const Capsule& capsule, const NS::Math::AABB& box) noexcept
    {
        const float minX = box.Center.x - box.Extents.x;
        const float maxX = box.Center.x + box.Extents.x;
        const float minY = box.Center.y - box.Extents.y;
        const float maxY = box.Center.y + box.Extents.y;
        const float minZ = box.Center.z - box.Extents.z;
        const float maxZ = box.Center.z + box.Extents.z;

        const float cx = capsule.center.x;
        const float cy = capsule.center.y;
        const float cz = capsule.center.z;
        const float hh = capsule.halfHeight;

        // 芯は縦線分なので X / Z は点、 Y だけ [cy-hh, cy+hh] の範囲を持つ
        // 各軸で box の外側にはみ出したぶんのギャップを取り、 二乗距離を radius と比較する
        const float gapX = std::max({minX - cx, 0.0f, cx - maxX});
        const float gapZ = std::max({minZ - cz, 0.0f, cz - maxZ});
        const float gapY = std::max({minY - (cy + hh), 0.0f, (cy - hh) - maxY});

        const float r = capsule.radius;
        return gapX * gapX + gapY * gapY + gapZ * gapZ <= r * r;
    }
} // namespace NS::Physics
