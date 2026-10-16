#include "NSlib/CoreObject/IUse/IUseCamera.h"
#include "NSlib/Core/Assert.h"

#include "NSlib/CoreObject/System/CameraManager.h"

namespace NS
{
    bool AddCameraModifier(const IUseCamera* user, std::unique_ptr<CameraModifier> modifier)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        CameraManager* cameras = user->GetCameraManager();
        return cameras != nullptr && cameras->AddModifier(std::move(modifier));
    }

    bool StartCameraShake(const IUseCamera* user, const CameraShakeDesc& desc)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        CameraManager* cameras = user->GetCameraManager();
        return cameras != nullptr && cameras->StartShake(desc);
    }

    bool StartCameraZoomRoll(const IUseCamera* user, const CameraZoomRollDesc& desc)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        CameraManager* cameras = user->GetCameraManager();
        return cameras != nullptr && cameras->StartZoomRoll(desc);
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
        return cameras != nullptr && cameras->AddTrauma(desc);
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
            cameras->RemoveModifiers(CameraShakeModifier::StaticKind());
            cameras->RemoveModifiers(CameraSinkModifier::StaticKind());
            cameras->RemoveModifiers(CameraZoomRollModifier::StaticKind());
            cameras->RemoveModifiers(CameraNudgeModifier::KindFor(false));
            cameras->RemoveModifiers(CameraNudgeModifier::KindFor(true));
        }
    }

    float CameraSideSignOf(const IUseCamera* user, const Vector3& direction) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const CameraManager* cameras = user->GetCameraManager();
        if (cameras == nullptr)
        {
            return 1.0f;
        }
        return cameras->SideSignOf(direction);
    }

    Vector3 CameraForwardHorizontal(const IUseCamera* user) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const CameraManager* cameras = user->GetCameraManager();
        if (cameras == nullptr)
        {
            return Vector3{0.0f, 0.0f, 1.0f};
        }
        return cameras->ForwardHorizontal();
    }

    std::optional<CameraPose> CameraViewPose(const IUseCamera* user) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const CameraManager* cameras = user->GetCameraManager();
        if (cameras == nullptr)
        {
            return std::nullopt;
        }
        return cameras->ViewPose();
    }

    void RegisterVirtualCamera(const IUseCamera* user, VirtualCamera* vcam)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (CameraManager* cameras = user->GetCameraManager())
        {
            cameras->AddVirtualCamera(vcam);
        }
    }

    void UnregisterVirtualCamera(const IUseCamera* user, VirtualCamera* vcam) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (CameraManager* cameras = user->GetCameraManager())
        {
            cameras->RemoveVirtualCamera(vcam);
        }
    }
} // namespace NS
