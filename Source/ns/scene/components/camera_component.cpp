#include "ns/scene/components/camera_component.h"

#include "ns/graphics/renderer.h"

#include <cmath>

namespace ns::scene
{
    void CameraComponent::SetPosition(const ns::core::Vector3& position) noexcept
    {
        m_camera.SetPosition(position);
    }

    void CameraComponent::SetTarget(const ns::core::Vector3& target) noexcept
    {
        m_camera.SetTarget(target);
    }

    void CameraComponent::SetUp(const ns::core::Vector3& up) noexcept
    {
        m_camera.SetUp(up);
    }

    void CameraComponent::SetFovY(float radians) noexcept
    {
        m_camera.SetFovY(radians);
    }

    void CameraComponent::SetAspectRatio(float aspect) noexcept
    {
        m_camera.SetAspectRatio(aspect);
    }

    void CameraComponent::SetAspectRatioFromRenderer(const ns::graphics::Renderer& renderer) noexcept
    {
        const int w = renderer.Width();
        const int h = renderer.Height();
        const float aspect = (w <= 0 || h <= 0) ? (16.0f / 9.0f) : static_cast<float>(w) / static_cast<float>(h);
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

    ns::core::Vector3 CameraComponent::ForwardHorizontal() const noexcept
    {
        const ns::core::Vector3 d = m_camera.Target() - m_camera.Position();
        const float lenSq = d.x * d.x + d.z * d.z;
        if (lenSq < 1e-8f)
            return ns::core::Vector3{0.0f, 0.0f, 1.0f};
        const float invLen = 1.0f / std::sqrt(lenSq);
        return ns::core::Vector3{d.x * invLen, 0.0f, d.z * invLen};
    }
} // namespace ns::scene
