#include "Runtime/Graphics/Camera.h"

namespace NS::Graphics
{
    namespace
    {
        NS::Core::Matrix MakeViewLH(const NS::Core::Vector3& position,
                                    const NS::Core::Vector3& target,
                                    const NS::Core::Vector3& up) noexcept
        {
            const DirectX::XMVECTOR eye = DirectX::XMLoadFloat3(&position);
            const DirectX::XMVECTOR tgt = DirectX::XMLoadFloat3(&target);
            const DirectX::XMVECTOR upv = DirectX::XMLoadFloat3(&up);
            NS::Core::Matrix m;
            DirectX::XMStoreFloat4x4(&m, DirectX::XMMatrixLookAtLH(eye, tgt, upv));
            return m;
        }

        NS::Core::Matrix MakeProjectionLH(float fovY, float aspect, float nearPlane, float farPlane) noexcept
        {
            NS::Core::Matrix m;
            DirectX::XMStoreFloat4x4(&m, DirectX::XMMatrixPerspectiveFovLH(fovY, aspect, nearPlane, farPlane));
            return m;
        }
    } // namespace

    Camera::Camera() noexcept : Camera(CameraDesc{}) {}

    Camera::Camera(const CameraDesc& desc) noexcept
        : m_position(desc.position), m_target(desc.target), m_up(desc.up), m_fovY(desc.fovY),
          m_aspect(desc.aspectRatio), m_near(desc.nearPlane), m_far(desc.farPlane), m_view(NS::Core::Matrix::Identity),
          m_projection(NS::Core::Matrix::Identity), m_viewDirty(true), m_projDirty(true)
    {}

    void Camera::SetPosition(const NS::Core::Vector3& position) noexcept
    {
        m_position = position;
        m_viewDirty = true;
    }
    void Camera::SetTarget(const NS::Core::Vector3& target) noexcept
    {
        m_target = target;
        m_viewDirty = true;
    }
    void Camera::SetUp(const NS::Core::Vector3& up) noexcept
    {
        m_up = up;
        m_viewDirty = true;
    }

    void Camera::SetFovY(NS::Core::Radians fov) noexcept
    {
        m_fovY = fov;
        m_projDirty = true;
    }
    void Camera::SetAspectRatio(float aspect) noexcept
    {
        m_aspect = aspect;
        m_projDirty = true;
    }
    void Camera::SetNearPlane(float nearPlane) noexcept
    {
        m_near = nearPlane;
        m_projDirty = true;
    }
    void Camera::SetFarPlane(float farPlane) noexcept
    {
        m_far = farPlane;
        m_projDirty = true;
    }

    const NS::Core::Vector3& Camera::Position() const noexcept
    {
        return m_position;
    }
    const NS::Core::Vector3& Camera::Target() const noexcept
    {
        return m_target;
    }
    const NS::Core::Vector3& Camera::Up() const noexcept
    {
        return m_up;
    }
    NS::Core::Radians Camera::FovY() const noexcept
    {
        return m_fovY;
    }
    float Camera::AspectRatio() const noexcept
    {
        return m_aspect;
    }
    float Camera::NearPlane() const noexcept
    {
        return m_near;
    }
    float Camera::FarPlane() const noexcept
    {
        return m_far;
    }

    const NS::Core::Matrix& Camera::View() const noexcept
    {
        if (m_viewDirty)
        {
            const NS::Core::Vector3 lookDir = m_target - m_position;
            if (lookDir.LengthSquared() < NS::Core::k_Epsilon * NS::Core::k_Epsilon)
            {
                m_view = NS::Core::Matrix::Identity;
            }
            else
            {
                m_view = MakeViewLH(m_position, m_target, m_up);
            }
            m_viewDirty = false;
        }
        return m_view;
    }

    const NS::Core::Matrix& Camera::Projection() const noexcept
    {
        if (m_projDirty)
        {
            m_projection = MakeProjectionLH(m_fovY.value, m_aspect, m_near, m_far);
            m_projDirty = false;
        }
        return m_projection;
    }

    NS::Core::Matrix Camera::ViewProjection() const noexcept
    {
        return View() * Projection();
    }

} // namespace NS::Graphics
