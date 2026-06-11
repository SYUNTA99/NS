#include "Framework/Scene/Components/PoleComponent.h"

#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Transform.h"

#include <cmath>

namespace NS::Scene
{
    PoleComponent::PoleComponent(float radius, float height) noexcept : m_radius(radius), m_height(height)
    {
        if (m_radius < 0.0f)
            m_radius = 0.0f;
        if (m_height < 0.0f)
            m_height = 0.0f;
    }

    NS::Math::Vector3 PoleComponent::AxisStart() const noexcept
    {
        NS::Math::Vector3 center{0.0f, 0.0f, 0.0f};
        if (const GameObject* owner = Owner(); owner != nullptr)
        {
            center = owner->Root().WorldMatrix().Translation();
        }
        return NS::Math::Vector3{center.x, center.y - m_height * 0.5f, center.z};
    }

    NS::Math::Vector3 PoleComponent::AxisEnd() const noexcept
    {
        NS::Math::Vector3 center{0.0f, 0.0f, 0.0f};
        if (const GameObject* owner = Owner(); owner != nullptr)
        {
            center = owner->Root().WorldMatrix().Translation();
        }
        return NS::Math::Vector3{center.x, center.y + m_height * 0.5f, center.z};
    }

    bool PoleComponent::ContainsPoint(const NS::Math::Vector3& worldPos) const noexcept
    {
        const NS::Math::Vector3 axisStart = AxisStart();
        const NS::Math::Vector3 axisEnd = AxisEnd();
        if (worldPos.y < axisStart.y || worldPos.y > axisEnd.y)
            return false;

        const float dx = worldPos.x - axisStart.x;
        const float dz = worldPos.z - axisStart.z;
        const float distSq = dx * dx + dz * dz;
        return distSq <= m_radius * m_radius;
    }
} // namespace NS::Scene
