#include "NSlib/CoreObject/IUse/IUseCamera.h"
#include "NSlib/Core/Assert.h"

#include "NSlib/CoreObject/System/CameraManager.h"

namespace NS
{
    bool AddCameraModifier(const IUseCamera* user, std::unique_ptr<CameraModifier> modifier)
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

    bool StartCameraShake(const IUseCamera* user, const CameraShakeDesc& desc)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        CameraManager* cameras = user->GetCameraManager();
        return cameras != nullptr && cameras->StartShake(desc, user);
    }

    bool StartCameraZoomRoll(const IUseCamera* user, const CameraZoomRollDesc& desc)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        CameraManager* cameras = user->GetCameraManager();
        return cameras != nullptr && cameras->StartZoomRoll(desc, user);
    }

    void StopCameraEffects(const IUseCamera* user) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (CameraManager* cameras = user->GetCameraManager())
        {
            cameras->ClearModifiers();
        }
    }

    bool AddCameraTrauma(const IUseCamera* user, const CameraTraumaDesc& desc)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        CameraManager* cameras = user->GetCameraManager();
        return cameras != nullptr && cameras->AddTrauma(desc, user);
    }

    bool HoldCameraTrauma(const IUseCamera* user, float level, const CameraTraumaShape& shape)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        CameraManager* cameras = user->GetCameraManager();
        return cameras != nullptr && cameras->HoldTrauma(level, shape);
    }

    void StopCameraHitEffects(const IUseCamera* user) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (CameraManager* cameras = user->GetCameraManager())
        {
            for (const void* kind : CameraHitEffectKinds())
            {
                cameras->RemoveModifiers(kind);
            }
        }
    }

    float CalcCameraSideSign(const IUseCamera* user, const Vector3& direction) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const CameraManager* cameras = user->GetCameraManager();
        if (cameras == nullptr)
        {
            return 1.0f;
        }
        return cameras->SideSignOf(direction);
    }

    Vector3 CalcCameraForwardHorizontal(const IUseCamera* user) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const CameraManager* cameras = user->GetCameraManager();
        if (cameras == nullptr)
        {
            return Vector3{0.0f, 0.0f, 1.0f};
        }
        return cameras->ForwardHorizontal();
    }

    std::optional<CameraPose> GetCameraViewPose(const IUseCamera* user) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const CameraManager* cameras = user->GetCameraManager();
        if (cameras == nullptr)
        {
            return std::nullopt;
        }
        return cameras->ViewPose();
    }

    void InitVirtualCamera(const IUseCamera* user, VirtualCamera* vcam)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, vcam != nullptr, "InitVirtualCamera: 仮想カメラが null");
        if (CameraManager* cameras = user->GetCameraManager())
        {
            cameras->AddVirtualCamera(vcam);
        }
    }

    void EnableVirtualCamera(const IUseCamera* user, const VirtualCamera* vcam) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, vcam != nullptr, "EnableVirtualCamera: 仮想カメラが null");
        if (CameraManager* cameras = user->GetCameraManager())
        {
            cameras->SetVirtualCameraEnabled(vcam, true);
        }
    }

    void DisableVirtualCamera(const IUseCamera* user, const VirtualCamera* vcam) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, vcam != nullptr, "DisableVirtualCamera: 仮想カメラが null");
        if (CameraManager* cameras = user->GetCameraManager())
        {
            cameras->SetVirtualCameraEnabled(vcam, false);
        }
    }

    void EndVirtualCamera(const IUseCamera* user, const VirtualCamera* vcam) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, vcam != nullptr, "EndVirtualCamera: 仮想カメラが null");
        if (CameraManager* cameras = user->GetCameraManager())
        {
            cameras->RemoveVirtualCamera(vcam);
        }
    }
} // namespace NS
