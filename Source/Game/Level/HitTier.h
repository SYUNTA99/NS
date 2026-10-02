#pragma once

namespace NS::Game::Level
{
    //! @brief 相手の中心からの横ずれで分けた当たりの段
    //! @details CollisionInput が横ずれ 0..1 を段へ分け、ImpactResolver が段で配分と演出を決める
    //! Replay の記録 "tier" と台本の期待はこの番号で段を読むので、番号を変えると台本の意味が変わる
    //! 1 は欠番。惜しいの段を消した時に外れの番号を詰めず、撮ってある記録と台本の意味を保った。段を足すなら 3 から
    enum class HitTier
    {
        Center = 0, //!< 中心近く
        Wide = 2,   //!< 大きな外れ
    };
} // namespace NS::Game::Level
