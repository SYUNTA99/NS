#pragma once

#include "NSlib/Core/Math.h"

namespace NS
{
    class Body;
}

namespace GL::Player
{
        //! @brief 体の水平の速度を単位方向へ加速し、横滑りを減らす。上限を超える順方向速度は保つ
        //! @details 曲がり方の手触りを決める式なので、基盤の Body でなくゲームに置く
        //! @param[in,out] body 事前条件: nullptr でない
        //! @param[in] direction 水平の単位方向
        //! @param[in] turningDrag 横滑りを減らす量 (m/s²)
        //! @param[in] acceleration 順方向の加速度 (m/s²)
        //! @param[in] topSpeed 加速で届く水平の速さの上限 (m/s)
        //! @param[in] dt 秒
        void Accelerate(
            NS::Body* body, const NS::Vector3& direction, float turningDrag, float acceleration, float topSpeed,
            float dt) noexcept;

        //! @brief 体の水平の速さを 1 フレームに deceleration × dt ずつ減らし、ちょうど 0 で止める。縦は触らない
        //! @param[in,out] body 事前条件: nullptr でない
        void Decelerate(NS::Body* body, float deceleration, float dt) noexcept;
} // namespace GL::Player
