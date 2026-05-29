#include "Framework/Scene/SlopeColliderComponent.h"

#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Transform.h"

#include <cmath>

namespace
{
    constexpr float kPi = 3.14159265358979323846f;
} // namespace

namespace NS::Scene
{
    SlopeColliderComponent::SlopeColliderComponent(NS::Scene::GameObject* owner,
                                                   float angleDegrees,
                                                   const NS::Core::Vector3& halfExtents) noexcept
        : Component(owner), m_angleDegrees(angleDegrees), m_halfExtents(halfExtents)
    {
        if (m_halfExtents.x < 0.0f)
            m_halfExtents.x = 0.0f;
        if (m_halfExtents.y < 0.0f)
            m_halfExtents.y = 0.0f;
        if (m_halfExtents.z < 0.0f)
            m_halfExtents.z = 0.0f;
    }

    std::array<NS::Physics::Triangle, 2> SlopeColliderComponent::WorldTriangles() const noexcept
    {
        const float ex = m_halfExtents.x;
        const float ey = m_halfExtents.y;
        const float ez = m_halfExtents.z;

        const float rawHeight = std::tan(m_angleDegrees * (kPi / 180.0f)) * (2.0f * ez);
        const float height = (rawHeight > 2.0f * ey) ? 2.0f * ey : rawHeight;

        const float yBottom = -ey;
        const float yTop = -ey + height;

        // Local 座標で 4 つの slope 角を定義する。 +Z 方向に上昇 (test 用 MakeWedgeSlope と同一規約)。
        const NS::Core::Vector3 lowLeft{-ex, yBottom, -ez};
        const NS::Core::Vector3 lowRight{ex, yBottom, -ez};
        const NS::Core::Vector3 highLeft{-ex, yTop, ez};
        const NS::Core::Vector3 highRight{ex, yTop, ez};

        // CCW (外側 = +Y, -Z 寄りから見て反時計回り) の 2 triangle 構成。
        std::array<NS::Physics::Triangle, 2> tris{
            NS::Physics::Triangle{lowLeft, highRight, lowRight},
            NS::Physics::Triangle{lowLeft, highLeft, highRight},
        };

        if (const GameObject* owner = Owner(); owner != nullptr)
        {
            const NS::Core::Matrix world = owner->Root().WorldMatrix();
            for (auto& tri : tris)
            {
                tri.v0 = NS::Core::Vector3::Transform(tri.v0, world);
                tri.v1 = NS::Core::Vector3::Transform(tri.v1, world);
                tri.v2 = NS::Core::Vector3::Transform(tri.v2, world);
            }
        }
        return tris;
    }
} // namespace NS::Scene
