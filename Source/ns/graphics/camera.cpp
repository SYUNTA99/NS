#include "ns/graphics/camera.h"

#include <DirectXMath.h>

namespace ns::graphics
{
    namespace
    {
        ns::core::Matrix MakeViewLH(const ns::core::Vector3& position,
                                    const ns::core::Vector3& target,
                                    const ns::core::Vector3& up) noexcept
        {
            const DirectX::XMVECTOR eye = DirectX::XMLoadFloat3(&position);
            const DirectX::XMVECTOR tgt = DirectX::XMLoadFloat3(&target);
            const DirectX::XMVECTOR upv = DirectX::XMLoadFloat3(&up);
            ns::core::Matrix m;
            DirectX::XMStoreFloat4x4(&m, DirectX::XMMatrixLookAtLH(eye, tgt, upv));
            return m;
        }

        ns::core::Matrix MakeProjectionLH(float fovY, float aspect, float nearPlane, float farPlane) noexcept
        {
            ns::core::Matrix m;
            DirectX::XMStoreFloat4x4(&m, DirectX::XMMatrixPerspectiveFovLH(fovY, aspect, nearPlane, farPlane));
            return m;
        }
    } // namespace

    Camera::Camera() noexcept
        : m_position(0.0f, 0.0f, -5.0f), m_target(0.0f, 0.0f, 0.0f), m_up(0.0f, 1.0f, 0.0f),
          m_fovY(ns::core::Deg2Rad(60.0f)), m_aspect(16.0f / 9.0f), m_near(0.1f), m_far(1000.0f),
          m_view(ns::core::Matrix::Identity), m_projection(ns::core::Matrix::Identity), m_viewDirty(true),
          m_projDirty(true)
    {}

    void Camera::SetPosition(const ns::core::Vector3& position) noexcept
    {
        m_position = position;
        m_viewDirty = true;
    }
    void Camera::SetTarget(const ns::core::Vector3& target) noexcept
    {
        m_target = target;
        m_viewDirty = true;
    }
    void Camera::SetUp(const ns::core::Vector3& up) noexcept
    {
        m_up = up;
        m_viewDirty = true;
    }

    void Camera::SetFovY(float radians) noexcept
    {
        m_fovY = radians;
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

    const ns::core::Vector3& Camera::Position() const noexcept
    {
        return m_position;
    }
    const ns::core::Vector3& Camera::Target() const noexcept
    {
        return m_target;
    }
    const ns::core::Vector3& Camera::Up() const noexcept
    {
        return m_up;
    }
    float Camera::FovY() const noexcept
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

    const ns::core::Matrix& Camera::View() const noexcept
    {
        if (m_viewDirty)
        {
            const ns::core::Vector3 lookDir = m_target - m_position;
            if (lookDir.LengthSquared() < 1e-8f)
            {
                m_view = ns::core::Matrix::Identity;
            }
            else
            {
                m_view = MakeViewLH(m_position, m_target, m_up);
            }
            m_viewDirty = false;
        }
        return m_view;
    }

    const ns::core::Matrix& Camera::Projection() const noexcept
    {
        if (m_projDirty)
        {
            m_projection = MakeProjectionLH(m_fovY, m_aspect, m_near, m_far);
            m_projDirty = false;
        }
        return m_projection;
    }

    ns::core::Matrix Camera::ViewProjection() const noexcept
    {
        return View() * Projection();
    }

} // namespace ns::graphics
