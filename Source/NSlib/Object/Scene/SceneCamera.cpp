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

    void SceneCamera::SetAspectRatioFromRenderer(const NS::Gfx::Renderer& renderer) noexcept
    {
        const NS::Size2D size = renderer.Size();
        if (size.width <= 0 || size.height <= 0)
        {
            m_camera.SetAspectRatio(16.0f / 9.0f);
            return;
        }
        m_camera.SetAspectRatio(NS::AspectRatio(size));
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
