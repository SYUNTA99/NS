#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/CameraModifier.h"

#include <memory>
#include <optional>

namespace NS
{
    class CameraManager;
    class VirtualCamera;

    //! 所属シーンのカメラ管理を取得するインターフェース
    class IGetCamera
    {
    public:
        //! カメラの管理役。シーンに居ない間は nullptr
        [[nodiscard]] virtual CameraManager* GetCameraManager() const noexcept = 0;

    protected:
        ~IGetCamera() = default;
    };

    // 効果を始める関数は user を頼んだ物 (CameraModifier::Requester) として積む。
    // 管理役は同じ頼んだ物の同じ種類だけを置き換え、頼んだ物ごとに外せる

    //! モディファイアを積む。管理役が無ければ何もせず false
    bool AddCameraModifier(const IGetCamera* user, std::unique_ptr<CameraModifier> modifier);

    //! 寄りと傾きを始める。管理役が無いか壊れた設定なら false
    bool StartCameraZoomRoll(const IGetCamera* user, const CameraZoomRollDesc& desc);

    //! 積んだ効果を、頼んだ物を問わず全て止める
    void StopCameraEffects(const IGetCamera* user) noexcept;

    //! トラウマを足す。管理役が無いか壊れた量なら false
    bool AddCameraTrauma(const IGetCamera* user, const CameraTraumaDesc& desc);

    //! 当たりで始めた揺れ・ずれ・寄りを、頼んだ物を問わず止める。トラウマは残す
    //! 次の当たりは前の当たりの揺れと寄りを止めてから始めるが、トラウマは続けて当てると足される
    void StopCameraHitEffects(const IGetCamera* user) noexcept;

    //! 今のカメラの画面で direction が右と左のどちらの側か。左なら -1、それ以外と管理役が無い時は 1
    [[nodiscard]] float CalcCameraSideSign(const IGetCamera* user, const Vector3& direction) noexcept;

    //! 遊びが読む水平の前。効果を掛ける前の視点から作る。管理役か選べる仮想カメラが無ければ +Z
    [[nodiscard]] Vector3 CalcCameraForwardHorizontal(const IGetCamera* user) noexcept;

    //! 効果を掛ける前の、遊びが読む視点。管理役か選べる仮想カメラが無ければ nullopt
    [[nodiscard]] std::optional<CameraPose> GetCameraViewPose(const IGetCamera* user) noexcept;

    // 仮想カメラは、アクターを作る時 (OnInit) に部品を作る行の隣で InitVirtualCamera を 1 回呼んで入れ、
    // アクターが終わる時 (OnEndPlay) に EndVirtualCamera で外す。出る・消える時は入れたまま
    // EnableVirtualCamera・DisableVirtualCamera で切り替える。管理役が無ければどれも何もしない

    //! 仮想カメラを管理役の候補へ、選べる物として入れる
    //! vcam は EndVirtualCamera まで生きていること
    void InitVirtualCamera(const IGetCamera* user, VirtualCamera* vcam);

    //! 入れた仮想カメラを選べる物にする。入れていない物は何もしない
    void EnableVirtualCamera(const IGetCamera* user, const VirtualCamera* vcam) noexcept;

    //! 入れた仮想カメラを、入れたまま選ばない。入れていない物は何もしない
    void DisableVirtualCamera(const IGetCamera* user, const VirtualCamera* vcam) noexcept;

    //! 仮想カメラを管理役の候補から外す。入れていない物は何もしない
    void EndVirtualCamera(const IGetCamera* user, const VirtualCamera* vcam) noexcept;
} // namespace NS
