#pragma once

#include "Runtime/Core/Math.h"

namespace NS::Game::Player
{
    //! @brief 反動の軌道のうち、当たりが決める向きと高さと距離
    //! @details 指示付き初期化で組み、Player::BeginRebound へ渡す。
    //! 上りと下りの重力と頂点の帯は Player が調整値の欄から決める。
    //! 衝突の配分の計算 (ImpactOutcome) が作り、Player が受けるので、どちらも読める独立のヘッダに置く
    struct ReboundArc
    {
        NS::Core::Vector3 direction{1.0f, 0.0f, 0.0f}; // 弾かれる向き。水平の成分だけを使う
        float apexHeight = 0.0f;                       // 弾かれ始めの高さから頂点までの高さ (m)
        float distance = 0.0f;                         // 弾かれ始めから同じ高さへ戻るまでの水平の距離 (m)
    };
} // namespace NS::Game::Player
