#include "Runtime/Object/IUse/IUseCamera.h"

#include "Runtime/Object/Components/CameraManager.h"

namespace NS::Obj
{
    bool AddCameraModifier(const IUseCamera& user, std::unique_ptr<CameraModifier> modifier)
    {
        CameraManager* cameras = user.GetCameraManager();
        return cameras != nullptr && cameras->AddModifier(std::move(modifier));
    }

    bool StartCameraShake(const IUseCamera& user, const CameraShakeDesc& desc)
    {
        CameraManager* cameras = user.GetCameraManager();
        return cameras != nullptr && cameras->StartShake(desc);
    }

    bool StartCameraZoomRoll(const IUseCamera& user, const CameraZoomRollDesc& desc)
    {
        CameraManager* cameras = user.GetCameraManager();
        return cameras != nullptr && cameras->StartZoomRoll(desc);
    }

    void StopCameraEffects(const IUseCamera& user) noexcept
    {
        if (CameraManager* cameras = user.GetCameraManager())
        {
            cameras->ClearModifiers();
        }
    }

    bool AddCameraTrauma(const IUseCamera& user, const CameraTraumaDesc& desc)
    {
        CameraManager* cameras = user.GetCameraManager();
        if (cameras == nullptr)
        {
            return false;
        }
        return cameras->AddTrauma(desc);
    }

    bool HoldCameraTrauma(const IUseCamera& user, float level, const CameraTraumaShape& shape)
    {
        CameraManager* cameras = user.GetCameraManager();
        if (cameras == nullptr)
        {
            return false;
        }
        return cameras->HoldTrauma(level, shape);
    }

    void StopCameraHitEffects(const IUseCamera& user) noexcept
    {
        if (CameraManager* cameras = user.GetCameraManager())
        {
            cameras->RemoveModifiers(CameraShakeModifier::StaticKind());
            cameras->RemoveModifiers(CameraZoomRollModifier::StaticKind());
        }
    }

    float CameraSideSignOf(const IUseCamera& user, const NS::Core::Vector3& direction) noexcept
    {
        const CameraManager* cameras = user.GetCameraManager();
        if (cameras == nullptr)
        {
            return 1.0f;
        }
        return cameras->SideSignOf(direction);
    }

    NS::Core::Vector3 CameraForwardHorizontal(const IUseCamera& user) noexcept
    {
        const CameraManager* cameras = user.GetCameraManager();
        if (cameras == nullptr)
        {
            return NS::Core::Vector3{0.0f, 0.0f, 1.0f};
        }
        return cameras->ForwardHorizontal();
    }

    std::optional<CameraPose> CameraViewPose(const IUseCamera& user) noexcept
    {
        const CameraManager* cameras = user.GetCameraManager();
        if (cameras == nullptr)
        {
            return std::nullopt;
        }
        return cameras->ViewPose();
    }

    void RegisterVirtualCamera(const IUseCamera& user, VirtualCamera* vcam)
    {
        if (CameraManager* cameras = user.GetCameraManager())
        {
            cameras->AddVirtualCamera(vcam);
        }
    }

    void UnregisterVirtualCamera(const IUseCamera& user, VirtualCamera* vcam) noexcept
    {
        if (CameraManager* cameras = user.GetCameraManager())
        {
            cameras->RemoveVirtualCamera(vcam);
        }
    }
} // namespace NS::Obj
