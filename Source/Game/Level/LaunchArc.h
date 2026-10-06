#pragma once

#include "NSlib/Core/Math.h"

namespace NS::Game::Level
{
    //! @details Player::ReboundVelocityFor も自機の反動の初速をこの形の式で出す
    //! 上りは riseGravity で減速する。強さは飛ばす側の調整値から渡す
    //! 下りの重力は上りの重力の fallGravityScale 倍
    //! 縦の速さの大きさが頂点の帯の縦速度より小さい間は、その時の重力にさらに帯の重力倍率を掛ける
    //! 水平は一定の速さで進み、発射の高さへ戻った所で飛ぶ距離だけ進んでいる
    struct LaunchArc
    {
        NS::Vector3 direction{1.0f, 0.0f, 0.0f}; // 飛ぶ向き。水平の成分だけを使う
        float distance = 0.0f;                         // 発射から発射の高さへ戻るまでの水平の距離 (m)
        float apexHeight = 0.0f;                       // 発射の高さから頂点までの高さ (m)
        // 上りの重力の大きさ (m/s²)
        float riseGravity = 25.0f;
        float fallGravityScale = 1.0f;     // 下りの重力 ÷ 上りの重力
        float apexBandSpeed = 0.0f;        // 頂点の帯の縦速度 (m/s)。0 なら帯は無い
        float apexBandGravityScale = 1.0f; // 頂点の帯の間に重力へ掛ける倍率
    };

    //! @brief 曲線の発射の瞬間の速度 (m/s) を返す
    //! @details 水平は飛ぶ距離 ÷ 発射の高さへ戻るまでの秒、上向きは頂点の高さへちょうど届く速さ
    //! 距離・高さ・上りの重力・2 つの倍率が有限の正でない時、帯の縦速度が有限の 0 以上でない時、
    //! 向きに水平の成分が無い時は 0 を返す
    [[nodiscard]] NS::Vector3 LaunchArcInitialVelocity(const LaunchArc& arc) noexcept;

    //! @brief 発射から seconds 秒後の、発射の位置からのずれを返す
    //! @details 水平の成分は arc.direction の水平の向きへ進んだずれ、y は発射の高さからの高さ。単位は m
    //! seconds が有限の 0 以上でない時と、arc が LaunchArcInitialVelocity の挙げる曲線にならない値の時は 0 を返す
    [[nodiscard]] NS::Vector3 LaunchArcOffsetAt(const LaunchArc& arc, float seconds) noexcept;

    //! @brief 発射から発射の高さへ戻るまでの秒を返す
    //! @details 水平は一定の速さで進むので、飛ぶ距離 ÷ 水平の速さ。
    //! arc が LaunchArcInitialVelocity の挙げる曲線にならない値の時は 0 を返す
    [[nodiscard]] float LaunchArcFlightSeconds(const LaunchArc& arc) noexcept;

} // namespace NS::Game::Level
