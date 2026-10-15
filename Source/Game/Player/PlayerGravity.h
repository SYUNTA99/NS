#pragma once

#include <cmath>

namespace GL
{
    namespace Player
    {
        //! @brief PlayerParams から取得する重力の調整値
        //! @details 自機の移動と突進の予測で、同じ重力を選ぶために使う
        struct PlayerGravity
        {
            float rise = -25.0f;    //!< 上向きの間の重力 (m/s²)。下向きが負。欄「上昇重力」
            float fall = -35.0f;    //!< 上向きでない間の重力 (m/s²)。下向きが負。欄「下降重力」
            float apexSpeed = 1.0f; //!< 縦の速さの大きさがこれ未満の間を頂点付近とする (m/s)。欄「頂点滞空 Vy」
            float apexScale = 0.5f; //!< 頂点付近で重力へ掛ける倍率。欄「頂点滞空倍率」
        };

        //! 上昇中は rise、それ以外は fall を選ぶ。頂点付近は apexScale を掛ける
        //! verticalVelocity は m/s、戻り値は m/s²。上が正
        [[nodiscard]] inline float ChooseGravity(const PlayerGravity& gravity, float verticalVelocity) noexcept
        {
            float strength = gravity.fall;
            if (verticalVelocity > 0.0f)
            {
                strength = gravity.rise;
            }
            if (std::abs(verticalVelocity) < gravity.apexSpeed)
            {
                strength *= gravity.apexScale;
            }
            return strength;
        }
    } // namespace Player
} // namespace GL
