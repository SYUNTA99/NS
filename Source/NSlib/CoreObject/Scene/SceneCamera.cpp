#include "NSlib/CoreObject/Scene/SceneCamera.h"

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/VirtualCamera.h"
#include "NSlib/Gfx/Renderer.h"

namespace NS
{
    void SceneCamera::SetPosition(const Vector3& position) noexcept
    {
        m_camera.SetPosition(position);
    }

    void SceneCamera::SetTarget(const Vector3& target) noexcept
    {
        m_camera.SetTarget(target);
    }

    void SceneCamera::SetUp(const Vector3& up) noexcept
    {
        m_camera.SetUp(up);
    }

    void SceneCamera::SetAspectRatioFromRenderer(const Gfx::Renderer& renderer) noexcept
    {
        const Size2D size = renderer.Size();
        if (size.width <= 0 || size.height <= 0)
        {
            m_camera.SetAspectRatio(16.0f / 9.0f);
            return;
        }
        m_camera.SetAspectRatio(AspectRatio(size));
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

} // namespace NS
