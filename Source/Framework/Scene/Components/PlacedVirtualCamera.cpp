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
        if (Owner() != nullptr)
            Owner()->Root().SetPosition(position);
        m_target = target;
    }

    NS::Math::Vector3 PlacedVirtualCamera::ViewPosition() const noexcept
    {
        if (Owner() != nullptr)
            return Owner()->Root().Position();
        return NS::Math::Vector3{0.0f, 5.0f, -10.0f};
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
        return MakePose(ViewPosition(), m_target, m_up);
    }

    NS_REGISTER_COMPONENT(PlacedVirtualCamera)
} // namespace NS::Scene
