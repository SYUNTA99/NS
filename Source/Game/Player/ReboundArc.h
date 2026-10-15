#pragma once

#include "NSlib/Core/Math.h"

#include <optional>

namespace GL::Player
{
    //! @brief 外れの反動の回り方のうち、当たりが決める物
    //! @details 回転の速さ・補間・軸のぶれは PlayerAppearance の欄で調整する
    struct MissTumble
    {
        //! かすった所の摩擦でねじれる軸。触れた面の向き × 滑る向き で、長さは端の近さ 0〜1。真ん中の外れは 0
        NS::Vector3 twist{0.0f, 0.0f, 0.0f};
        float power = 0.0f; //!< 当たりの最終威力。ねじれの速さに掛ける
    };

    //! @brief 当たりで決まる反動の向き・高さ・距離
    //! @details 重力は PlayerActor の調整値で決め、BeginRebound に渡す
    struct ReboundArc
    {
        NS::Vector3 direction{1.0f, 0.0f, 0.0f}; // 弾かれる向き。水平の成分だけを使う
        float apexHeight = 0.0f;                       // 弾かれ始めの高さから頂点までの高さ (m)
        float distance = 0.0f;                         // 弾かれ始めから同じ高さへ戻るまでの水平の距離 (m)
        // 外れの時だけ持つ。玉が軸のぶれるねじれで回り、着地の後にこすって止まる
        std::optional<MissTumble> missTumble{};
    };
} // namespace GL::Player
