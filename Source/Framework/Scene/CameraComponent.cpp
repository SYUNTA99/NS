#include "Framework/Scene/CameraComponent.h"

#include "Framework/Graphics/Renderer.h"
#include "Framework/Scene/GameObject.h"

#include <cmath>

namespace NS::Scene
{
    CameraComponent::CameraComponent(NS::Scene::GameObject* owner) noexcept : Component(owner) {}

    void CameraComponent::SetPosition(const NS::Core::Vector3& position) noexcept
    {
        m_camera.SetPosition(position);
    }

    void CameraComponent::SetTarget(const NS::Core::Vector3& target) noexcept
    {
        m_camera.SetTarget(target);
    }

    void CameraComponent::SetUp(const NS::Core::Vector3& up) noexcept
    {
        m_camera.SetUp(up);
    }

    void CameraComponent::SetFovY(NS::Core::Radians fov) noexcept
    {
        m_camera.SetFovY(fov);
    }

    void CameraComponent::SetAspectRatio(float aspect) noexcept
    {
        m_camera.SetAspectRatio(aspect);
    }

    void CameraComponent::SetAspectRatioFromRenderer(const NS::Graphics::Renderer& renderer) noexcept
    {
        const NS::Core::Size2D size = renderer.Size();
        const float aspect = (size.width <= 0 || size.height <= 0) ? (16.0f / 9.0f) : NS::Core::AspectRatio(size);
        m_camera.SetAspectRatio(aspect);
    }

    void CameraComponent::SetNearPlane(float nearPlane) noexcept
    {
        m_camera.SetNearPlane(nearPlane);
    }

    void CameraComponent::SetFarPlane(float farPlane) noexcept
    {
        m_camera.SetFarPlane(farPlane);
    }

    NS::Core::Vector3 CameraComponent::ForwardHorizontal() const noexcept
    {
        const NS::Core::Vector3 d = m_camera.Target() - m_camera.Position();
        const float lenSq = d.x * d.x + d.z * d.z;
        if (lenSq < 1e-8f)
            return NS::Core::Vector3{0.0f, 0.0f, 1.0f};
        const float invLen = 1.0f / std::sqrt(lenSq);
        return NS::Core::Vector3{d.x * invLen, 0.0f, d.z * invLen};
    }
} // namespace NS::Scene
