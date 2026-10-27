#pragma once

#include "Game/Level/AirGravity.h"
#include "NSlib/Core/Math.h"

namespace GL::Level
{
        //! @brief 発射の高さへ戻るまでの距離と頂点の高さで指定する飛行曲線
        //! @details 水平は等速。上下と頂点付近の重力は各欄で調整する
        struct LaunchArc
        {
            NS::Vector3 direction{1.0f, 0.0f, 0.0f}; // 飛ぶ向き。水平の成分だけを使う
            float distance = 0.0f;                   // 発射から発射の高さへ戻るまでの水平の距離 (m)
            float apexHeight = 0.0f;                 // 発射の高さから頂点までの高さ (m)
            // 上りの重力の大きさ (m/s²)
            float riseGravity = 25.0f;
            float fallGravityScale = 1.0f;     // 下りの重力 ÷ 上りの重力
            float apexBandSpeed = 0.0f;        // 頂点の帯の縦速度 (m/s)。0 なら帯は無い
            float apexBandGravityScale = 1.0f; // 頂点の帯の間に重力へ掛ける倍率
        };

        //! @brief 相手ごとの飛び方。飛ぶ距離と質量で割る強さは自機の欄が持ち、ここには無い
        struct LaunchShape
        {
            float apexHeight{};           //!< 質量 1・威力 1 の飛ぶ高さ。単位は m
            float riseGravity{};          //!< 上りの重力。単位は m/s²
            float fallGravityScale{};     //!< 下りの重力 ÷ 上りの重力
            float apexBandSpeed{};        //!< 頂点の帯の縦速度。単位は m/s
            float apexBandGravityScale{}; //!< 頂点の帯の間に重力へ掛ける倍率
            float missHeightRatio{};      //!< 外れで、弧の高さに距離と同じ縮みの上から掛ける割合
            float missDistanceRatio{};    //!< 外れで、距離に押し込む成分の縮みの上から掛ける割合
        };

        //! 発射時の速度 (m/s) を返す
        //! 距離・高さ・重力・倍率が有限の正でない、帯速度が有限の非負でない、水平の向きが無い時は 0
        [[nodiscard]] NS::Vector3 LaunchArcInitialVelocity(const LaunchArc& arc) noexcept;

        //! @brief 発射から seconds 秒後の、発射の位置からのずれを返す
        //! @details 水平の成分は arc.direction の水平の向きへ進んだずれ、y は発射の高さからの高さ。単位は m
        //! seconds が有限の 0 以上でない時と、arc が LaunchArcInitialVelocity の挙げる曲線にならない値の時は 0 を返す
        [[nodiscard]] NS::Vector3 LaunchArcOffsetAt(const LaunchArc& arc, float seconds) noexcept;

        //! 発射の高さへ戻るまでの秒を返す。曲線が不正なら 0
        [[nodiscard]] float LaunchArcFlightSeconds(const LaunchArc& arc) noexcept;

        //! 曲線の重力を、ChooseGravity へ渡す組 (下向きが負) にして返す
        [[nodiscard]] AirGravity LaunchArcGravity(const LaunchArc& arc) noexcept;
} // namespace GL::Level
