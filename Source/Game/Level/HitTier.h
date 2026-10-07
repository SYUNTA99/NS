#pragma once

#include <array>

namespace NS::Game::Level
{
    //! @brief 相手の正面の面のどこに当てたかで分けた当たりの段
    //! 当たりの記録 "tier" と台本の期待はこの番号で段を読むので、番号を変えると台本の意味が変わる
    //! 1 は欠番。撮ってある記録と台本の意味を保つため、外れの番号を詰めない。段を足すなら 3 から
    enum class HitTier
    {
        Center = 0, //!< 中心近く
        Wide = 2,   //!< 大きな外れ
    };

    // 段を足したらここにも足す
    [[nodiscard]] inline std::array<HitTier, 2> HitTiers() noexcept
    {
        return {HitTier::Center, HitTier::Wide};
    }
} // namespace NS::Game::Level
