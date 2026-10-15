#pragma once

#include "NSlib/Core/Math.h"

#include "NSlib/Core/Types.h"

namespace GL
{
    namespace Level
    {
        //! 外れで飛んだ相手が着地のたびに跳ねる時の値
        struct MissHopDesc
        {
            float heightRatio = 0.35f; //!< 跳ねる上向きの速さの、跳ねた後の水平の速さに対する割合の上限
            float turnDegrees = 60.0f; //!< 跳ねる向きが左右へぶれる角度の上限 (度)
            float keep = 0.7f;         //!< 跳ねるたびに残る水平の速さの割合
        };

        //! 着地後の跳ねる速度 (m/s) を返す。水平速度が 0 なら 0
        //! up は正規化した反重力方向、hopIndex は 0 始まり。同じ seed・番号では同じ跳ね方になる
        [[nodiscard]] NS::Vector3 MissHopVelocity(const NS::Vector3& velocity,
                                                  const NS::Vector3& up,
                                                  const MissHopDesc& desc,
                                                  NS::uint32 seed,
                                                  int hopIndex) noexcept;
    } // namespace Level
} // namespace GL
