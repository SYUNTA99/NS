#include "NSlib/Object/Scene/SceneCamera.h"

#include "NSlib/Core/Math.h"
#include "NSlib/Graphics/Renderer.h"
#include "NSlib/Object/Components/VirtualCamera.h"

namespace NS::Obj
{
    void SceneCamera::SetPosition(const NS::Vector3& position) noexcept
    {
        m_camera.SetPosition(position);
    }

    void SceneCamera::SetTarget(const NS::Vector3& target) noexcept
    {
        m_camera.SetTarget(target);
    }

    void SceneCamera::SetUp(const NS::Vector3& up) noexcept
    {
        m_camera.SetUp(up);
    }

    void SceneCamera::SetFovY(NS::Radians fov) noexcept
    {
        m_camera.SetFovY(fov);
    }

    void SceneCamera::SetAspectRatio(float aspect) noexcept
    {
        m_camera.SetAspectRatio(aspect);
    }

    void SceneCamera::SetAspectRatioFromRenderer(const NS::Gfx::Renderer& renderer) noexcept
    {
        const NS::Size2D size = renderer.Size();
        const float aspect = [&]() -> float {
            if (size.width <= 0 || size.height <= 0)
            {
                return 16.0f / 9.0f;
            }
            return NS::AspectRatio(size);
        }();
        m_camera.SetAspectRatio(aspect);
    }

    void SceneCamera::SetNearPlane(float nearPlane) noexcept
    {
        m_camera.SetNearPlane(nearPlane);
    }

    void SceneCamera::SetFarPlane(float farPlane) noexcept
    {
        m_camera.SetFarPlane(farPlane);
    }

    void SceneCamera::ApplyPose(const CameraPose& pose) noexcept
    {
        m_camera.SetPosition(pose.position);
        m_camera.SetTarget(pose.target);
        m_camera.SetUp(pose.up);
        m_camera.SetFovY(pose.fovY);
        m_camera.SetNearPlane(pose.nearPlane);
        m_camera.SetFarPlane(pose.farPlane);
        m_camera.SetScreenOffset(pose.screenOffset);
    }

} // namespace NS::Obj
