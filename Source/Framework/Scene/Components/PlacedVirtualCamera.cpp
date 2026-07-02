#include "Framework/Scene/Components/PlacedVirtualCamera.h"

#include "Framework/Scene/ComponentRegistry.h"
#include "Framework/Scene/GameObject.h"

#include <cmath>

namespace NS::Scene
{
    PlacedVirtualCamera::PlacedVirtualCamera() noexcept : VirtualCameraComponent(static_cast<int>(TickPriority::Camera))
    {}

    void PlacedVirtualCamera::SetView(const NS::Math::Vector3& position, const NS::Math::Vector3& target) noexcept
    {
        m_position = position;
        m_target = target;
    }

    void PlacedVirtualCamera::UpdateActivation(const NS::Math::Vector3& playerPosition) noexcept
    {
        const bool inside = std::abs(playerPosition.x - m_triggerCenter.x) <= m_triggerExtent.x &&
                            std::abs(playerPosition.y - m_triggerCenter.y) <= m_triggerExtent.y &&
                            std::abs(playerPosition.z - m_triggerCenter.z) <= m_triggerExtent.z;
        SetActive(inside);
        if (inside && m_lookAtPlayer)
            m_target = playerPosition;
    }

    CameraPose PlacedVirtualCamera::EvaluatePose(float /*alpha*/) const noexcept
    {
        return MakePose(m_position, m_target, m_up);
    }

    NS_REGISTER_COMPONENT(PlacedVirtualCamera)
} // namespace NS::Scene
