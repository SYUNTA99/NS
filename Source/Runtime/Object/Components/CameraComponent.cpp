#include "Runtime/Object/Components/CameraComponent.h"

#include "Runtime/Graphics/Renderer.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <cmath>

namespace NS::Object
{
    CameraComponent::CameraComponent() noexcept
        : Component(NS::Object::TickPriority::LateUpdate + 50)
    {}

    void CameraComponent::SetPosition(const NS::Math::Vector3& position) noexcept
    {
        m_camera.SetPosition(position);
    }

    void CameraComponent::SetTarget(const NS::Math::Vector3& target) noexcept
    {
        m_camera.SetTarget(target);
    }

    void CameraComponent::SetUp(const NS::Math::Vector3& up) noexcept
    {
        m_camera.SetUp(up);
    }

    void CameraComponent::SetFovY(NS::Math::Radians fov) noexcept
    {
        m_camera.SetFovY(fov);
    }

    void CameraComponent::SetAspectRatio(float aspect) noexcept
    {
        m_camera.SetAspectRatio(aspect);
    }

    void CameraComponent::SetAspectRatioFromRenderer(const NS::Graphics::Renderer& renderer) noexcept
    {
        const NS::Math::Size2D size = renderer.Size();
        const float aspect = [&]() -> float {
            if (size.width <= 0 || size.height <= 0)
                return 16.0f / 9.0f;
            return NS::Math::AspectRatio(size);
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

    NS::Math::Vector3 CameraComponent::ForwardHorizontal() const noexcept
    {
        const NS::Math::Vector3 d = m_camera.Target() - m_camera.Position();
        const float lenSq = d.x * d.x + d.z * d.z;
        if (lenSq < 1e-8f)
            return NS::Math::Vector3{0.0f, 0.0f, 1.0f};
        const float invLen = 1.0f / std::sqrt(lenSq);
        return NS::Math::Vector3{d.x * invLen, 0.0f, d.z * invLen};
    }

    NS_CLASS(CameraComponent)
} // namespace NS::Object
