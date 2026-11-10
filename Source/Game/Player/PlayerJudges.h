#pragma once

#include "NSlib/Core/Math.h"

namespace GL
{
    namespace Player
    {
        //! 跳びの判定
        class PlayerJudgeJump
        {
        public:
            //! 接地かコヨーテの猶予の間で跳べる回数が残り、押したか先行入力の猶予の間の場合 true、それ以外の場合は
            //! false
            [[nodiscard]] static bool Judge(
                bool grounded, int coyoteRemaining, int jumpsRemaining, bool pressed, int bufferRemaining) noexcept
            {
                return (grounded || coyoteRemaining > 0) && jumpsRemaining > 0 && (pressed || bufferRemaining > 0);
            }
        };

        //! 縁を掴む判定
        class PlayerJudgeLedgeGrab
        {
        public:
            //! 宙にいて上昇していない場合 true、それ以外の場合は false
            [[nodiscard]] static bool Judge(bool grounded, float verticalVelocity) noexcept
            {
                return !grounded && !NS::IsPositive(verticalVelocity);
            }

            //! 縁の上面の高さが、手の高さの帯に入っているかを返す
            //! @param[in] handY 手の高さ。世界座標
            //! @param[in] top 縁の上面の高さ。世界座標
            //! @param[in] belowHand 手より下に許す上面の深さ。単位は m
            //! @param[in] aboveHand 手より上に許す上面の高さ。単位は m
            //! @return 入っている場合 true、それ以外の場合は false
            [[nodiscard]] static bool InBand(float handY, float top, float belowHand, float aboveHand) noexcept
            {
                return top >= handY - belowHand && top <= handY + aboveHand;
            }
        };

        //! 歩きの判定
        class PlayerJudgeWalk
        {
        public:
            //! 接地していて、入力があるか止まりきっていない場合 true、それ以外の場合は false
            [[nodiscard]] static bool Judge(bool grounded, bool hasInput, bool stopped) noexcept
            {
                return grounded && (hasInput || !stopped);
            }
        };

        //! 立ちの判定
        class PlayerJudgeIdle
        {
        public:
            [[nodiscard]] static bool Judge(bool grounded, bool walking) noexcept { return grounded && !walking; }

            //! 接地していて、移動入力がなく、水平に止まっている場合 true、それ以外の場合は false
            [[nodiscard]] static bool Judge(bool grounded, bool hasInput, bool stopped) noexcept
            {
                return grounded && !hasInput && stopped;
            }
        };

        //! 切り返しのブレーキの判定
        class PlayerJudgeBrake
        {
        public:
            //! 入力があり、入力の水平の向きへの水平の速度の成分が threshold 未満かを返す
            //! @return ブレーキを掛ける場合 true、それ以外の場合は false。入力の向きが水平に無い時は false
            [[nodiscard]] static bool Judge(bool hasInput,
                                            const NS::Vector3& desiredDirection,
                                            const NS::Vector3& lateralVelocity,
                                            float threshold) noexcept
            {
                NS::Vector3 direction{};
                if (!hasInput || !NS::TryNormalizeHorizontal(desiredDirection, direction))
                {
                    return false;
                }
                return NS::DotHorizontal(direction, lateralVelocity) < threshold;
            }
        };

        //! 着地の判定
        class PlayerJudgeLand
        {
        public:
            //! 接地していて上昇していない場合 true、それ以外の場合は false
            //! 反動の明けのフレームは止める前の接地のフラグが残ったまま上向きの速度が入る。
            //! 接地だけを見ると宙へ出る前に立ちへ移る
            [[nodiscard]] static bool Judge(bool grounded, float verticalVelocity) noexcept
            {
                return grounded && !NS::IsPositive(verticalVelocity);
            }
        };

        //! 落下の判定
        class PlayerJudgeFall
        {
        public:
            [[nodiscard]] static bool Judge(bool grounded) noexcept { return !grounded; }
        };

        //! スティックを倒しているかの判定
        class PlayerJudgeMoveInput
        {
        public:
            //! 倒し具合が遊び以上の場合 true、それ以外の場合は false
            [[nodiscard]] static bool Judge(float speedScale01, float deadzone) noexcept
            {
                return speedScale01 >= deadzone;
            }
        };

        //! 水平に止まっているかの判定
        class PlayerJudgeStopped
        {
        public:
            //! 水平の速度の x と z がどちらもちょうど 0 の場合 true、それ以外の場合は false
            [[nodiscard]] static bool Judge(const NS::Vector3& lateralVelocity) noexcept
            {
                return lateralVelocity.x == 0.0f && lateralVelocity.z == 0.0f;
            }
        };

        //! 縁をよじ登るかの判定
        class PlayerJudgeClimbLedge
        {
        public:
            [[nodiscard]] static bool Judge(float climbForward) noexcept { return climbForward > 0.0f; }
        };

        //! 縁を手放すかの判定
        class PlayerJudgeDropLedge
        {
        public:
            [[nodiscard]] static bool Judge(bool releasePressed) noexcept { return releasePressed; }
        };
    } // namespace Player
} // namespace GL
