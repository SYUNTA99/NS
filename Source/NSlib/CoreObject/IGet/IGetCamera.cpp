#include "NSlib/CoreObject/IGet/IGetCamera.h"
#include "NSlib/Core/Assert.h"

#include "NSlib/CoreObject/System/CameraManager.h"

#include <cmath>

namespace NS
{
    bool AddCameraModifier(const IGetCamera* user, std::unique_ptr<CameraModifier> modifier)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        CameraManager* cameras = user->GetCameraManager();
        if (cameras == nullptr || modifier == nullptr)
        {
            return false;
        }
        modifier->SetRequester(user);
        return cameras->AddModifier(std::move(modifier));
    }

    void StopCameraEffects(const IGetCamera* user) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (CameraManager* cameras = user->GetCameraManager())
        {
            cameras->ClearModifiers();
        }
    }

    bool AddCameraTrauma(const IGetCamera* user, const CameraTraumaDesc& desc)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        CameraManager* cameras = user->GetCameraManager();
        return cameras != nullptr && cameras->AddTrauma(desc, user);
    }

    float CalcCameraSideSign(const IGetCamera* user, const Vector3& direction) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const CameraManager* cameras = user->GetCameraManager();
        if (cameras == nullptr)
        {
            return 1.0f;
        }
        return cameras->SideSignOf(direction);
    }

    Vector3 CalcCameraForwardHorizontal(const IGetCamera* user) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const CameraManager* cameras = user->GetCameraManager();
        if (cameras == nullptr)
        {
            return Vector3{0.0f, 0.0f, 1.0f};
        }
        return cameras->ForwardHorizontal();
    }

    std::optional<CameraPose> GetCameraViewPose(const IGetCamera* user) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const CameraManager* cameras = user->GetCameraManager();
        if (cameras == nullptr)
        {
            return std::nullopt;
        }
        return cameras->ViewPose();
    }

    float ScreenPixelsToMeters(float pixels, const CameraPose& pose, const Vector3& at, float referenceHeight) noexcept
    {
        if (!std::isfinite(referenceHeight) || referenceHeight <= 0.0f)
        {
            return 0.0f;
        }
        Vector3 forward = pose.target - pose.position;
        if (!HasPositiveMagnitude(forward))
        {
            return 0.0f;
        }
        forward.Normalize();
        float depth = Dot(at - pose.position, forward);
        if (!IsPositive(depth))
        {
            depth = (at - pose.position).Length();
        }
        return pixels * 2.0f * depth * std::tan(pose.fovY.value * 0.5f) / referenceHeight;
    }

    void InitVirtualCamera(const IGetCamera* user, VirtualCamera* vcam)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, vcam != nullptr, "InitVirtualCamera: 仮想カメラが null");
        if (CameraManager* cameras = user->GetCameraManager())
        {
            cameras->AddVirtualCamera(vcam);
        }
    }

    void EnableVirtualCamera(const IGetCamera* user, const VirtualCamera* vcam) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, vcam != nullptr, "EnableVirtualCamera: 仮想カメラが null");
        if (CameraManager* cameras = user->GetCameraManager())
        {
            cameras->SetVirtualCameraEnabled(vcam, true);
        }
    }

    void DisableVirtualCamera(const IGetCamera* user, const VirtualCamera* vcam) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, vcam != nullptr, "DisableVirtualCamera: 仮想カメラが null");
        if (CameraManager* cameras = user->GetCameraManager())
        {
            cameras->SetVirtualCameraEnabled(vcam, false);
        }
    }

    void EndVirtualCamera(const IGetCamera* user, const VirtualCamera* vcam) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, vcam != nullptr, "EndVirtualCamera: 仮想カメラが null");
        if (CameraManager* cameras = user->GetCameraManager())
        {
            cameras->RemoveVirtualCamera(vcam);
        }
    }
} // namespace NS
