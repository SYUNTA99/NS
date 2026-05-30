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

    std::array<NS::Physics::Triangle, 8> SlopeColliderComponent::WorldTriangles() const noexcept
    {
        const float ex = m_halfExtents.x;
        const float ey = m_halfExtents.y;
        const float ez = m_halfExtents.z;

        const float rawHeight = std::tan(m_angleDegrees * (kPi / 180.0f)) * (2.0f * ez);
        const float height = (rawHeight > 2.0f * ey) ? 2.0f * ey : rawHeight;

        const float yBottom = -ey;
        const float yTop = -ey + height;

        // wedge の 6 頂点 (local)。 +Z 側が高い斜面。
        const NS::Core::Vector3 frontBotL{-ex, yBottom, -ez};
        const NS::Core::Vector3 frontBotR{ex, yBottom, -ez};
        const NS::Core::Vector3 backBotL{-ex, yBottom, ez};
        const NS::Core::Vector3 backBotR{ex, yBottom, ez};
        const NS::Core::Vector3 backTopL{-ex, yTop, ez};
        const NS::Core::Vector3 backTopR{ex, yTop, ez};

        // SweptCapsuleVsTriangle は CCW 規約 (cross(v1-v0, v2-v0) = 外向き法線)。
        // 全 5 面 = 8 triangle: 斜面 2 + 底面 2 + 裏壁 2 + 左右側面 1+1。
        std::array<NS::Physics::Triangle, 8> tris{
            NS::Physics::Triangle{frontBotL, backTopR, frontBotR}, // 斜面 +Y/-Z
            NS::Physics::Triangle{frontBotL, backTopL, backTopR},  // 斜面 +Y/-Z
            NS::Physics::Triangle{frontBotL, frontBotR, backBotR}, // 底 -Y
            NS::Physics::Triangle{frontBotL, backBotR, backBotL},  // 底 -Y
            NS::Physics::Triangle{backBotL, backBotR, backTopR},   // 裏壁 +Z
            NS::Physics::Triangle{backBotL, backTopR, backTopL},   // 裏壁 +Z
            NS::Physics::Triangle{frontBotL, backBotL, backTopL},  // 左側面 -X
            NS::Physics::Triangle{frontBotR, backTopR, backBotR},  // 右側面 +X
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
