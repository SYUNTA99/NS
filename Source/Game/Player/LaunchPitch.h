#pragma once

#include "Game/Level/AirGravity.h"

namespace GL
{
    namespace Player
    {
        /// 放った玉の道筋の入力
        /// 自機と同じ重力・順序で、速度を更新してから位置を進める
        struct LaunchPath
        {
            float horizontalSpeed = 0.0f;    // 水平の速さ (m/s)
            float verticalSpeed = 0.0f;      // 放つ瞬間の縦の速さ (m/s)。上が正
            GL::Level::AirGravity gravity{}; // 重力の強さを選ぶ欄の写し
            float dt = 0.0f;                 // 1 フレームの秒
            bool grounded = false;           // 接地して放つか。真なら道筋は放った高さより下へ行かない
            /// 道筋を予測するフレーム数の上限。欄「突進の予測フレーム上限」
            int maxFrames = 0;
        };

        /// 放った玉の中心が水平に distance 進んだ所での、放った高さからの高さを返す
        /// フレームの間は直線で結ぶ。接地して放った道筋は、放った高さまで戻るとそこで縦の速さを 0 にして
        /// 床の上を進む
        /// @param[in] path 道筋を決める値
        /// @param[in] distance 放った所からの水平の距離 (m)
        /// @return 放った高さからの高さ (m)。上が正。水平の速さか dt が有限の正でない時、distance が有限の 0
        /// 以上でない時、 縦の速さが有限でない時は 0
        [[nodiscard]] float LaunchHeightAt(const LaunchPath& path, float distance) noexcept;

        /// 放つ上下の速さを探す時の入力
        struct LaunchPitchDesc
        {
            float ballHeight = 0.0f;         // 放つ時の玉の中心の高さ (世界の y)
            float targetHeight = 0.0f;       // 相手に触れる所で玉の中心を着けたい高さ (世界の y)
            float contactDistance = 0.0f;    // 玉の中心が相手に触れる所までの水平の距離 (m)
            float horizontalSpeed = 0.0f;    // 突進の水平の速さ (m/s)
            GL::Level::AirGravity gravity{}; // 重力の強さを選ぶ欄の写し
            /// 放つ角度の上限 (度)。上向きも下向きもこの角度で切る。欄「放つ角度の上限」
            float maxAngleDegrees = 0.0f;
            bool grounded = false; // 接地しているか。真なら下へ向けない
            float dt = 0.0f;       // 1 フレームの秒
            /// 着きたい高さに対する誤差の許容幅。欄「突進の高さ探索の誤差」
            float heightTolerance = 0.0f;
            /// 直角で計算が発散する手前の角度上限。欄「突進の角度計算の上限」
            float angleGuardDegrees = 0.0f;
            /// 道筋を予測するフレーム数の上限。欄「突進の予測フレーム上限」
            int maxFrames = 0;
        };

        /// LaunchPitch の結果
        struct LaunchPitchResult
        {
            float verticalSpeed = 0.0f; // 放つ瞬間の縦の速さ (m/s)。上が正。届かない時は 0
            bool reachable = false;     // 上限の角度の中で着きたい高さに着く場合 true
        };

        /// 指定高さに届く縦速度を、LaunchHeightAt と同じ道筋で探す。接地中は下向きに放たない
        /// 不正な入力か角度上限内で届かない場合は reachable が false、縦速度は 0
        [[nodiscard]] LaunchPitchResult LaunchPitch(const LaunchPitchDesc& desc) noexcept;
    } // namespace Player
} // namespace GL
