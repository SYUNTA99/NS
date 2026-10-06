#pragma once

#include "NSlib/Core/Math.h"

#include <cstdint>

namespace NS::Game::Level
{
    //! 外れで飛んだ相手が着地のたびに跳ねる時の値
    struct MissHopDesc
    {
        float heightRatio = 0.35f; //!< 跳ねる上向きの速さの、跳ねた後の水平の速さに対する割合の上限
        float turnDegrees = 60.0f; //!< 跳ねる向きが左右へぶれる角度の上限 (度)
        float keep = 0.7f;         //!< 跳ねるたびに残る水平の速さの割合
    };

    //! @brief 外れで飛んだ相手が着地した時の、跳ねた直後の速度を返す
    //! @details 水平の速さに desc.keep を掛け、向きを up の周りに ±desc.turnDegrees の中で回す。上向きの速さは
    //! 跳ねた後の水平の速さ × desc.heightRatio × 0.5〜1。ぶれと高さは seed と hopIndex だけで決まり、同じ当たりは
    //! 同じ跳ね方になる。水平の速さが 0 の時は 0 を返す
    //! @param[in] velocity 着地した時の速度 (m/s)
    //! @param[in] up 重力の逆の向き。正規化済み
    //! @param[in] desc 跳ね方を決める値
    //! @param[in] seed 当たりごとの種
    //! @param[in] hopIndex 何回目の跳ねか。0 から数える
    //! @return 跳ねた直後の速度 (m/s)
    [[nodiscard]] NS::Vector3 MissHopVelocity(const NS::Vector3& velocity,
                                                    const NS::Vector3& up,
                                                    const MissHopDesc& desc,
                                                    std::uint32_t seed,
                                                    int hopIndex) noexcept;
} // namespace NS::Game::Level
