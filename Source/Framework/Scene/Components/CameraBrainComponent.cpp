#include "Framework/Scene/Components/CameraBrainComponent.h"

#include "Framework/Scene/Components/CameraComponent.h"
#include "Framework/Scene/Components/VirtualCameraComponent.h"

#include <algorithm>

namespace NS::Scene
{
    CameraBrainComponent::CameraBrainComponent() noexcept : Component(static_cast<int>(TickPriority::Camera)) {}

    void CameraBrainComponent::AddVirtualCamera(VirtualCameraComponent* vcam)
    {
        if (vcam == nullptr)
            return;
        if (std::find(m_vcams.begin(), m_vcams.end(), vcam) != m_vcams.end())
            return;
        m_vcams.push_back(vcam);
    }

    VirtualCameraComponent* CameraBrainComponent::SelectActive() const noexcept
    {
        VirtualCameraComponent* best = nullptr;
        for (auto* vcam : m_vcams)
        {
            if (vcam == nullptr || !vcam->IsActive())
                continue;
            if (best == nullptr || vcam->VcamPriority() > best->VcamPriority())
                best = vcam;
        }
        return best;
    }

    void CameraBrainComponent::Evaluate(float alpha) noexcept
    {
        m_active = SelectActive();
        if (m_active == nullptr || m_camera == nullptr)
            return;

        const CameraPose pose = m_active->EvaluatePose(alpha);
        m_camera->SetPosition(pose.position);
        m_camera->SetTarget(pose.target);
        m_camera->SetUp(pose.up);
        m_camera->SetFovY(pose.fovY);
        m_camera->SetNearPlane(pose.nearPlane);
        m_camera->SetFarPlane(pose.farPlane);
    }

    NS::Math::Matrix CameraBrainComponent::ViewProjection() const noexcept
    {
        return (m_camera != nullptr) ? m_camera->ViewProjection() : NS::Math::Matrix::Identity;
    }

    NS::Math::Vector3 CameraBrainComponent::ForwardHorizontal() const noexcept
    {
        return (m_camera != nullptr) ? m_camera->ForwardHorizontal() : NS::Math::Vector3{0.0f, 0.0f, 1.0f};
    }
} // namespace NS::Scene
