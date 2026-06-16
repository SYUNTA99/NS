#include "Framework/Scene/Components/CameraBrainComponent.h"

#include "Framework/Core/Clock.h"
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

    void CameraBrainComponent::RemoveVirtualCamera(VirtualCameraComponent* vcam) noexcept
    {
        if (vcam == nullptr)
            return;
        m_vcams.erase(std::remove(m_vcams.begin(), m_vcams.end(), vcam), m_vcams.end());
        if (m_active == vcam)
            m_active = nullptr; // 次の OnUpdate / Evaluate で選び直す
    }

    void CameraBrainComponent::SetBlendDuration(float seconds) noexcept
    {
        m_blendDuration = (seconds > 0.0f) ? seconds : 0.0f;
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

    void CameraBrainComponent::OnUpdate()
    {
        VirtualCameraComponent* next = SelectActive();
        if (next != m_active)
        {
            // 直前まで写していた pose から新 vcam へ繋ぐ。初回 active 化 (旧 pose 無し) はカット
            if (m_active != nullptr && m_blendDuration > 0.0f)
            {
                m_blendFrom = m_lastPose;
                m_blendElapsed = 0.0f;
                m_blending = true;
            }
            m_active = next;
        }

        if (m_blending)
        {
            m_blendElapsed += NS::Core::FrameTimer::FixedDelta();
            if (m_blendElapsed >= m_blendDuration)
                m_blending = false;
        }
    }

    void CameraBrainComponent::Evaluate(float alpha) noexcept
    {
        if (m_active == nullptr)
            m_active = SelectActive(); // OnUpdate より先に render が来た初回フレーム用の保険
        if (m_active == nullptr || m_camera == nullptr)
            return;

        CameraPose pose = m_active->EvaluatePose(alpha);
        if (m_blending && m_blendDuration > 0.0f)
        {
            const float t = NS::Math::Clamp(m_blendElapsed / m_blendDuration, 0.0f, 1.0f);
            const float eased = t * t * (3.0f - 2.0f * t); // smoothstep で ease-in-out
            pose = CameraPose::Lerp(m_blendFrom, pose, eased);
        }

        m_lastPose = pose;
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
