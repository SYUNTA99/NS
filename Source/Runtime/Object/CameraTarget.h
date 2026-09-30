#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/ThirdPersonFollow.h"

namespace NS::Obj
{
    //! @brief 追従カメラが追う相手が 1 フレームぶん出す状態
    struct CameraTargetState
    {
        bool grounded = true;                         //!< 接地しているか。自動ズームの判定に使う
        NS::Core::Vector3 velocity{0.0f, 0.0f, 0.0f}; //!< 速度 (m/s)
        float heightOffset = 0.0f;                    //!< 根から見る所までの高さのずれ (m)
        bool hasRebound = false;                      //!< 反動の状態を出すか
        FollowReboundDesc rebound{};                  //!< 反動の状態
        bool hasCharge = false;                       //!< 溜めの状態を出すか
        FollowChargeDesc charge{};                    //!< 溜めの状態
    };

    //! @brief 追従カメラに追われる物が持つ窓口。オデッセイの CameraTargetBase に当たる
    //! @details 追う側は相手の部品を読まず、相手がこの窓口で答えた状態だけを使う
    //! Actor::GetCameraTarget が返す。追われない物は nullptr を返す
    class ICameraTarget
    {
    public:
        //! 今のフレームの状態
        [[nodiscard]] virtual CameraTargetState GetCameraTargetState() const = 0;

    protected:
        ~ICameraTarget() = default;
    };
} // namespace NS::Obj
