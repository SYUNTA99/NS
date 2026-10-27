#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/ThirdPersonFollow.h"

namespace GL
{
    namespace Player
    {
        //! 追従カメラが追う相手が 1 フレームぶん出す状態
        struct CameraTargetState
        {
            bool grounded = true;                   // 接地しているか。自動ズームの判定に使う
            NS::Vector3 velocity{0.0f, 0.0f, 0.0f}; // 速度 (m/s)
            float heightOffset = 0.0f;              // 根から見る所までの高さのずれ (m)
            bool hasRebound = false;
            NS::FollowReboundDesc rebound{};
            bool hasCharge = false;
            NS::FollowChargeDesc charge{};
            //! 体当たりから止めの明けまでか。真の間、カメラは距離と溜めの締めをその時のまま保つ
            bool framingHeld = false;
        };
    } // namespace Player
} // namespace GL
