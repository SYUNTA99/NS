#pragma once

#include <array>

namespace GL::Level
{
        //! @brief 相手の正面の当たり位置で決まる段
        //! 記録と台本の互換性のため番号を固定する。1 は欠番で、新しい段は 3 以降
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
} // namespace GL::Level
