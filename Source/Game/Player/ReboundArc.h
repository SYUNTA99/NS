#pragma once

#include "Runtime/Core/Math.h"

#include <optional>

namespace NS::Game::Player
{
    //! @brief 外れの反動の回り方のうち、当たりが決める物
    //! @details 速さの欄と、回転を寄せるフレーム数・軸のぶれは Player の欄が持つ
    struct MissTumble
    {
        //! かすった所の摩擦でねじれる軸。触れた面の向き × 滑る向き で、長さは端の近さ 0〜1。真ん中の外れは 0
        NS::Core::Vector3 twist{0.0f, 0.0f, 0.0f};
        float power = 0.0f; //!< 当たりの最終威力。ねじれの速さに掛ける
    };

    //! @brief 反動の軌道のうち、当たりが決める向きと高さと距離
    //! @details 指示付き初期化で組み、Player::BeginRebound へ渡す。
    //! 上りと下りの重力と頂点の帯は Player が調整値の欄から決める。
    //! 衝突の配分の計算 (ImpactOutcome) が作り、Player が受けるので、どちらも読める独立のヘッダに置く
    struct ReboundArc
    {
        NS::Core::Vector3 direction{1.0f, 0.0f, 0.0f}; // 弾かれる向き。水平の成分だけを使う
        float apexHeight = 0.0f;                       // 弾かれ始めの高さから頂点までの高さ (m)
        float distance = 0.0f;                         // 弾かれ始めから同じ高さへ戻るまでの水平の距離 (m)
        // 外れの時だけ持つ。玉が軸のぶれるねじれで回り、着地の後にこすって止まる
        std::optional<MissTumble> missTumble{};
    };
} // namespace NS::Game::Player
