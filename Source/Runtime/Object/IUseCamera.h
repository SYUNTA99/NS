#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/CameraModifier.h"

#include <memory>
#include <optional>

namespace NS::Obj
{
    class CameraManager;
    class VirtualCamera;

    //! @brief カメラの窓口。シーンに 1 つのカメラの管理役 (CameraManager) を引ける物が持つ
    //! @details Actor・UIActor・シーンが持つ。部品は持ち主の Actor から引く
    //! 揺れや寄りのような効果は、下の補助関数で管理役へモディファイアを積む。管理役の中身は触らない
    class IUseCamera
    {
    public:
        //! カメラの管理役。シーンに居ない間は nullptr
        [[nodiscard]] virtual CameraManager* GetCameraManager() const noexcept = 0;

    protected:
        ~IUseCamera() = default;
    };

    //! モディファイアを積む。管理役が無ければ何もせず false
    bool AddCameraModifier(const IUseCamera& user, std::unique_ptr<CameraModifier> modifier);

    //! 画面揺れを始める。管理役が無いか壊れた設定なら false
    bool StartCameraShake(const IUseCamera& user, const CameraShakeDesc& desc);

    //! 寄りと傾きを始める。管理役が無いか壊れた設定なら false
    bool StartCameraZoomRoll(const IUseCamera& user, const CameraZoomRollDesc& desc);

    //! 積んだ効果を全て止める
    void StopCameraEffects(const IUseCamera& user) noexcept;

    //! 今のカメラの画面で direction が右と左のどちらの側か。左なら -1、それ以外と管理役が無い時は 1
    [[nodiscard]] float CameraSideSignOf(const IUseCamera& user, const NS::Core::Vector3& direction) noexcept;

    //! 実カメラの水平の前。管理役が無ければ +Z
    [[nodiscard]] NS::Core::Vector3 CameraForwardHorizontal(const IUseCamera& user) noexcept;

    //! 管理役が実カメラへ書く姿勢を、書かずに返す。管理役か選べる仮想カメラが無ければ nullopt
    [[nodiscard]] std::optional<CameraPose> ComposeCameraPose(const IUseCamera& user, float alpha) noexcept;

    //! 仮想カメラを管理役の候補へ入れる。管理役が無ければ何もしない
    void RegisterVirtualCamera(const IUseCamera& user, VirtualCamera* vcam);

    //! 仮想カメラを管理役の候補から外す。管理役が無ければ何もしない
    void UnregisterVirtualCamera(const IUseCamera& user, VirtualCamera* vcam) noexcept;
} // namespace NS::Obj
