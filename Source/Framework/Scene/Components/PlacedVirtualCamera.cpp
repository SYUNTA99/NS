#include "Framework/Scene/Components/PlacedVirtualCamera.h"

namespace NS::Scene
{
    PlacedVirtualCamera::PlacedVirtualCamera() noexcept : VirtualCameraComponent(static_cast<int>(TickPriority::Camera))
    {}

    void PlacedVirtualCamera::SetView(const NS::Math::Vector3& position, const NS::Math::Vector3& target) noexcept
    {
        m_position = position;
        m_target = target;
    }

    CameraPose PlacedVirtualCamera::EvaluatePose(float /*alpha*/) const noexcept
    {
        return MakePose(m_position, m_target, m_up);
    }
} // namespace NS::Scene
