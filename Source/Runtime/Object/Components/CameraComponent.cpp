#include "Runtime/Object/Components/CameraComponent.h"

#include "Runtime/Core/Math.h"
#include "Runtime/Graphics/Renderer.h"
#include "Runtime/Object/Components/VirtualCameraComponent.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

namespace NS::Object
{
    CameraComponent::CameraComponent() noexcept : Component(NS::Object::TickPriority::LateUpdate + 50) {}

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
        const float aspect = [&]() -> float {
            if (size.width <= 0 || size.height <= 0)
                return 16.0f / 9.0f;
            return NS::Core::AspectRatio(size);
        }();
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

    void CameraComponent::ApplyPose(const CameraPose& pose) noexcept
    {
        m_camera.SetPosition(pose.position);
        m_camera.SetTarget(pose.target);
        m_camera.SetUp(pose.up);
        m_camera.SetFovY(pose.fovY);
        m_camera.SetNearPlane(pose.nearPlane);
        m_camera.SetFarPlane(pose.farPlane);
    }

    NS::Core::Vector3 CameraComponent::ForwardHorizontal() const noexcept
    {
        const NS::Core::Vector3 d = m_camera.Target() - m_camera.Position();
        NS::Core::Vector3 out{};
        if (!NS::Core::TryNormalizeHorizontal(d, out))
            return NS::Core::Vector3{0.0f, 0.0f, 1.0f};
        return out;
    }

    NS_CLASS(CameraComponent)
} // namespace NS::Object
