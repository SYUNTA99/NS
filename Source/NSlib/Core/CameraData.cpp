#include "NSlib/Core/CameraData.h"

namespace NS
{
    namespace
    {
        NS::Matrix MakeViewLH(const NS::Vector3& position,
                                    const NS::Vector3& target,
                                    const NS::Vector3& up) noexcept
        {
            const DirectX::XMVECTOR eye = DirectX::XMLoadFloat3(&position);
            const DirectX::XMVECTOR tgt = DirectX::XMLoadFloat3(&target);
            const DirectX::XMVECTOR upv = DirectX::XMLoadFloat3(&up);
            NS::Matrix m;
            DirectX::XMStoreFloat4x4(&m, DirectX::XMMatrixLookAtLH(eye, tgt, upv));
            return m;
        }

        NS::Matrix MakeProjectionLH(float fovY, float aspect, float nearPlane, float farPlane) noexcept
        {
            NS::Matrix m;
            DirectX::XMStoreFloat4x4(&m, DirectX::XMMatrixPerspectiveFovLH(fovY, aspect, nearPlane, farPlane));
            return m;
        }
    } // namespace

    void CameraData::SetPosition(const NS::Vector3& position) noexcept
    {
        m_position = position;
        m_viewDirty = true;
    }
    void CameraData::SetTarget(const NS::Vector3& target) noexcept
    {
        m_target = target;
        m_viewDirty = true;
    }
    void CameraData::SetUp(const NS::Vector3& up) noexcept
    {
        m_up = up;
        m_viewDirty = true;
    }

    void CameraData::SetFovY(NS::Radians fov) noexcept
    {
        m_fovY = fov;
        m_projDirty = true;
    }
    void CameraData::SetAspectRatio(float aspect) noexcept
    {
        m_aspect = aspect;
        m_projDirty = true;
    }
    void CameraData::SetNearPlane(float nearPlane) noexcept
    {
        m_near = nearPlane;
        m_projDirty = true;
    }
    void CameraData::SetFarPlane(float farPlane) noexcept
    {
        m_far = farPlane;
        m_projDirty = true;
    }
    void CameraData::SetScreenOffset(const NS::Vector2& offset) noexcept
    {
        m_screenOffset = offset;
        m_projDirty = true;
    }

    const NS::Vector3& CameraData::Position() const noexcept
    {
        return m_position;
    }
    const NS::Vector3& CameraData::Target() const noexcept
    {
        return m_target;
    }
    const NS::Vector3& CameraData::Up() const noexcept
    {
        return m_up;
    }
    NS::Radians CameraData::FovY() const noexcept
    {
        return m_fovY;
    }
    float CameraData::AspectRatio() const noexcept
    {
        return m_aspect;
    }
    float CameraData::NearPlane() const noexcept
    {
        return m_near;
    }
    float CameraData::FarPlane() const noexcept
    {
        return m_far;
    }
    const NS::Vector2& CameraData::ScreenOffset() const noexcept
    {
        return m_screenOffset;
    }

    const NS::Matrix& CameraData::View() const noexcept
    {
        if (m_viewDirty)
        {
            const NS::Vector3 lookDir = m_target - m_position;
            if (lookDir.LengthSquared() < NS::k_Epsilon * NS::k_Epsilon)
            {
                m_view = NS::Matrix::Identity;
            }
            else
            {
                m_view = MakeViewLH(m_position, m_target, m_up);
            }
            m_viewDirty = false;
        }
        return m_view;
    }

    const NS::Matrix& CameraData::Projection() const noexcept
    {
        if (m_projDirty)
        {
            m_projection = MakeProjectionLH(m_fovY.value, m_aspect, m_near, m_far);
            // 行ベクトルでは clip.w がビューの奥行きなので、3 行目に足した分は w で割った後にそのまま画面のずれになる
            m_projection._31 += m_screenOffset.x;
            m_projection._32 += m_screenOffset.y;
            m_projDirty = false;
        }
        return m_projection;
    }

    NS::Matrix CameraData::ViewProjection() const noexcept
    {
        return View() * Projection();
    }

} // namespace NS
