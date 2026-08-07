#include "Runtime/Physics/WedgeGeometry.h"

#include <algorithm>
#include <cmath>

namespace NS::Physics
{
    std::array<Triangle, 8> BuildWedgeTriangles(const NS::Core::Vector3& center,
                                                const NS::Core::Vector3& halfExtents,
                                                float angleDegrees,
                                                float yawRadians) noexcept
    {
        const float ex = halfExtents.x;
        const float ey = halfExtents.y;
        const float ez = halfExtents.z;

        const float rawHeight = std::tan(NS::Core::DegreesToRadians(angleDegrees)) * (2.0f * ez);
        const float height = std::min(rawHeight, 2.0f * ey);
        const float yBottom = -ey;
        const float yTop = -ey + height;

        // slope の transform 回転 CreateFromYawPitchRoll(yaw) と同じ yaw を使うことで
        // 描画 mesh と collider の向きが一致する
        const NS::Core::Matrix rot = NS::Core::Matrix::CreateRotationY(yawRadians);

        const auto place = [&](float x, float y, float z) {
            const NS::Core::Vector3 rotated = NS::Core::Vector3::Transform(NS::Core::Vector3{x, y, z}, rot);
            return NS::Core::Vector3{rotated.x + center.x, rotated.y + center.y, rotated.z + center.z};
        };

        const NS::Core::Vector3 fBL = place(-ex, yBottom, -ez);
        const NS::Core::Vector3 fBR = place(ex, yBottom, -ez);
        const NS::Core::Vector3 bBL = place(-ex, yBottom, ez);
        const NS::Core::Vector3 bBR = place(ex, yBottom, ez);
        const NS::Core::Vector3 bTL = place(-ex, yTop, ez);
        const NS::Core::Vector3 bTR = place(ex, yTop, ez);

        return {
            Triangle{fBL, bTR, fBR},
            Triangle{fBL, bTL, bTR},
            Triangle{fBL, fBR, bBR},
            Triangle{fBL, bBR, bBL},
            Triangle{bBL, bBR, bTR},
            Triangle{bBL, bTR, bTL},
            Triangle{fBL, bBL, bTL},
            Triangle{fBR, bTR, bBR},
        };
    }
} // namespace NS::Physics
