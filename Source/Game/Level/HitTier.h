#pragma once

namespace NS::Game::Level
{
    //! @brief 突進の線が相手のど真ん中をどれだけ通ったかで分けた当たりの段
    //! @details 相手の HitZones が線と体の中心の距離を範囲と比べて段を決め、ImpactResolver が段で演出を決める
    //! Replay の記録 "tier" と台本の期待はこの番号で段を読むので、番号を変えると台本の意味が変わる
    enum class HitTier
    {
        Center = 0, //!< 中心近く
        Near = 1,   //!< 惜しい
        Wide = 2,   //!< 大きな外れ
    };
} // namespace NS::Game::Level
