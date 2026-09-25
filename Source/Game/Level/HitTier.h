#pragma once

namespace NS::Game::Level
{
    //! @brief 相手の中心からの横ずれで分けた当たりの段
    //! @details CollisionInput が横ずれ 0..1 を段へ分け、ImpactResolver が段で演出を決める
    //! Replay の記録 "tier" と台本の期待はこの番号で段を読むので、番号を変えると台本の意味が変わる
    enum class HitTier
    {
        Center = 0, //!< 中心近く
        Near = 1,   //!< 惜しい
        Wide = 2,   //!< 大きな外れ
    };
} // namespace NS::Game::Level
