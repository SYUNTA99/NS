#pragma once

#include <cmath>

namespace NS::Game::Player
{
    //! @brief 自機の重力の強さを選ぶ欄の写し
    //! @details 値の正は PlayerParams の欄で、PlayerParams::Gravity が写して渡す。
    //! Player::Gravity と、放つ角度を決める LaunchPitch が同じ写しで強さを選ぶ。
    //! 反動の間は上りだけ倍率付きの PlayerParams::ReboundGravity を同じ選び方へ渡す
    struct PlayerGravity
    {
        float rise = -25.0f;    //!< 上向きの間の重力 (m/s²)。下向きが負。欄「上昇重力」
        float fall = -35.0f;    //!< 上向きでない間の重力 (m/s²)。下向きが負。欄「下降重力」
        float apexSpeed = 1.0f; //!< 縦の速さの大きさがこれ未満の間を頂点付近とする (m/s)。欄「頂点滞空 Vy」
        float apexScale = 0.5f; //!< 頂点付近で重力へ掛ける倍率。欄「頂点滞空倍率」
    };

    //! @brief 縦の速さから、この 1 フレームに当てる重力の強さを選ぶ
    //! @details 上向きなら rise、それ以外は fall。縦の速さの大きさが apexSpeed 未満なら apexScale を掛ける
    //! @param[in] gravity 重力の欄の写し
    //! @param[in] verticalVelocity 今の縦の速さ (m/s)。上が正
    //! @return 重力の強さ (m/s²)。下向きが負
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
} // namespace NS::Game::Player
