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
                bool grounded, float coyoteRemaining, int jumpsRemaining, bool pressed, float bufferRemaining) noexcept
            {
                return (grounded || coyoteRemaining > 0.0f) && jumpsRemaining > 0 &&
                       (pressed || bufferRemaining > 0.0f);
            }
        };

        //! 体当たりの発動の判定
        class PlayerJudgeBodySlam
        {
        public:
            //! @brief 要求の猶予が残り、spent と wasSlamming が偽で locomotion が真かを返す
            //! @param[in] locomotion 立ち・歩き・落ち・反動のどれかの状態か
            //! @return 発動する場合 true、それ以外の場合は false
            [[nodiscard]] static bool Judge(float bufferRemaining,
                                            bool spent,
                                            bool wasSlamming,
                                            bool locomotion) noexcept
            {
                return bufferRemaining > 0.0f && !spent && !wasSlamming && locomotion;
            }
        };

        //! 縁を掴む判定
        class PlayerJudgeLedgeGrab
        {
        public:
            //! 宙にいて上昇しておらず、突進中でない場合 true、それ以外の場合は false
            [[nodiscard]] static bool Judge(bool grounded, float verticalVelocity, bool wasSlamming) noexcept
            {
                return !grounded && !NS::IsPositive(verticalVelocity) && !wasSlamming;
            }

            //! @brief 縁の上面の高さが、手の高さの帯に入っているかを返す
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
            //! @brief 入力があり、入力の水平の向きへの水平の速度の成分が threshold 未満かを返す
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
            //! @brief 接地していて上昇していない場合 true、それ以外の場合は false
            //! @details 反動の明けのフレームは止める前の接地のフラグが残ったまま上向きの速度が入る。
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

        //! 通常移動の状態かの判定
        class PlayerJudgeLocomotion
        {
        public:
            struct StateFlags
            {
                bool idle = false;
                bool walk = false;
                bool fall = false;
                bool rebound = false;
            };

            [[nodiscard]] static bool Judge(const StateFlags& flags) noexcept
            {
                return flags.idle || flags.walk || flags.fall || flags.rebound;
            }

            [[nodiscard]] static bool Judge(bool idle, bool walk, bool fall, bool rebound) noexcept
            {
                return Judge(StateFlags{idle, walk, fall, rebound});
            }
        };
    } // namespace Player
} // namespace GL
