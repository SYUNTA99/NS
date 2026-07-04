#include "Framework/Physics/WedgeGeometry.h"

#include <cmath>

namespace NS::Physics
{
    std::array<Triangle, 8> BuildWedgeTriangles(const NS::Math::Vector3& center,
                                                const NS::Math::Vector3& halfExtents,
                                                float angleDegrees,
                                                float yawRadians) noexcept
    {
        const float ex = halfExtents.x;
        const float ey = halfExtents.y;
        const float ez = halfExtents.z;

        constexpr float kPi = 3.14159265358979323846f;
        const float rawHeight = std::tan(angleDegrees * (kPi / 180.0f)) * (2.0f * ez);
        const float height = (rawHeight > 2.0f * ey) ? 2.0f * ey : rawHeight;
        const float yBottom = -ey;
        const float yTop = -ey + height;

        // slope の transform 回転 CreateFromYawPitchRoll(yaw) と同じ yaw を使うことで
        // 描画 mesh と collider の向きが一致する
        const NS::Math::Matrix rot = NS::Math::Matrix::CreateRotationY(yawRadians);

        const auto place = [&](float x, float y, float z) {
            const NS::Math::Vector3 rotated = NS::Math::Vector3::Transform(NS::Math::Vector3{x, y, z}, rot);
            return NS::Math::Vector3{rotated.x + center.x, rotated.y + center.y, rotated.z + center.z};
        };

        const NS::Math::Vector3 fBL = place(-ex, yBottom, -ez);
        const NS::Math::Vector3 fBR = place(ex, yBottom, -ez);
        const NS::Math::Vector3 bBL = place(-ex, yBottom, ez);
        const NS::Math::Vector3 bBR = place(ex, yBottom, ez);
        const NS::Math::Vector3 bTL = place(-ex, yTop, ez);
        const NS::Math::Vector3 bTR = place(ex, yTop, ez);

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
