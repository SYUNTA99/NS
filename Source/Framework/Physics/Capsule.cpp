#include "Framework/Physics/Capsule.h"

#include <algorithm>

namespace NS::Physics
{
    bool IntersectsCapsuleAabb(const Capsule& capsule, const NS::Math::AABB& box) noexcept
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
