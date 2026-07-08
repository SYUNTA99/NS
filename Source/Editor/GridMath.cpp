#include "Editor/GridMath.h"


namespace NS::Editor
{

    NS::Math::Ray ScreenToWorldRay(const NS::Math::Matrix& viewProjection,
                                   NS::Math::Size2D viewport,
                                   int mouseX,
                                   int mouseY) noexcept
    {
        const float ndcX = (2.0f * static_cast<float>(mouseX)) / static_cast<float>(viewport.width) - 1.0f;
        const float ndcY = 1.0f - (2.0f * static_cast<float>(mouseY)) / static_cast<float>(viewport.height);

        NS::Math::Matrix inv = viewProjection.Invert();

        const NS::Math::Vector4 nearH{ndcX, ndcY, 0.0f, 1.0f};
        const NS::Math::Vector4 farH{ndcX, ndcY, 1.0f, 1.0f};

        const NS::Math::Vector4 wNearH = NS::Math::Vector4::Transform(nearH, inv);
        const NS::Math::Vector4 wFarH = NS::Math::Vector4::Transform(farH, inv);

        const float invWNear = [&]() -> float {
            if (std::abs(wNearH.w) > 1e-6f)
                return 1.0f / wNearH.w;
            return 0.0f;
        }();
        const float invWFar = [&]() -> float {
            if (std::abs(wFarH.w) > 1e-6f)
                return 1.0f / wFarH.w;
            return 0.0f;
        }();

        NS::Math::Vector3 wNear{wNearH.x * invWNear, wNearH.y * invWNear, wNearH.z * invWNear};
        NS::Math::Vector3 wFar{wFarH.x * invWFar, wFarH.y * invWFar, wFarH.z * invWFar};
        NS::Math::Vector3 dir = wFar - wNear;
        dir.Normalize();
        return NS::Math::Ray{wNear, dir};
    }

    NS::Math::Vector3 SnapWorldPointToGrid(NS::Math::Vector3 p, float g) noexcept
    {
        // 0.5 を足してから floor で四捨五入相当
        const float gx = std::floor(p.x / g + 0.5f) * g;
        const float gy = std::floor(p.y / g + 0.5f) * g;
        const float gz = std::floor(p.z / g + 0.5f) * g;
        return {gx, gy, gz};
    }

    NS::Math::Vector3 SnapHitToPlacementCell(NS::Math::Vector3 hit, NS::Math::Vector3 normal, float g) noexcept
    {
        const NS::Math::Vector3 base = SnapWorldPointToGrid(hit, g);
        return {base.x + normal.x * g, base.y + normal.y * g, base.z + normal.z * g};
    }

    bool TryGroundPlaneFallback(const NS::Math::Ray& ray, NS::Math::Vector3& outCenter, float g) noexcept
    {
        // 上向き ray / 水平 ray は地面に当たらない
        if (ray.direction.y > -1e-4f)
            return false;
        const float t = -ray.position.y / ray.direction.y;
        if (t < 0.0f)
            return false;
        const NS::Math::Vector3 hit{
            ray.position.x + ray.direction.x * t,
            0.0f,
            ray.position.z + ray.direction.z * t,
        };
        outCenter = SnapWorldPointToGrid(hit, g);
        outCenter.y = 0.0f;
        return true;
    }

    NS::Math::Quaternion RotationToQuaternion(std::uint8_t rotation) noexcept
    {
        const std::uint8_t r = static_cast<std::uint8_t>(rotation & 0x03);
        constexpr float kQuarter = 1.5707963267948966f;
        const float angle = static_cast<float>(r) * kQuarter;
        return NS::Math::Quaternion::CreateFromAxisAngle({0.0f, 1.0f, 0.0f}, angle);
    }

} // namespace NS::Editor
