#pragma once

namespace NS
{
    class ThirdPersonFollow;

    //! 追従カメラに追われるアクターが、追うのに要る自分の状態を渡すインターフェース
    //! 追従カメラ (ThirdPersonFollow) は追う相手の型を知らない。相手の状態の中身と、壊れた値の扱いは相手が決める
    class IGetCameraTarget
    {
    public:
        //! このフレームの自分の状態 (接地・速度・見る高さ・溜め・反動など) を vcam の欄へ書く
        //! 呼ぶのは CameraManager::OnTick の中の ThirdPersonFollow::Advance だけで、固定フレームに 1 回、
        //! vcam を進める直前に呼ぶ。止めている間 (Scene::HoldOthers) も呼ぶ
        //! @param[in] vcam 書く先の追従カメラ。事前条件: nullptr でない
        virtual void WriteFollowState(ThirdPersonFollow* vcam) const = 0;

    protected:
        ~IGetCameraTarget() = default;
    };
} // namespace NS
