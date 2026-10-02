#pragma once

#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Math.h"

namespace NS::Game::Player
{

    //! 跳びの判定
    class PlayerJudgeJump
    {
    public:
        //! 接地かコヨーテの猶予の間で跳べる回数が残り、押したか先行入力の猶予の間の場合 true、それ以外の場合は false
        [[nodiscard]] static bool Judge(
            bool grounded, float coyoteRemaining, int jumpsRemaining, bool pressed, float bufferRemaining) noexcept
        {
            return (grounded || coyoteRemaining > 0.0f) && jumpsRemaining > 0 && (pressed || bufferRemaining > 0.0f);
        }
    };

    //! 体当たりの発動の判定
    class PlayerJudgeBodySlam
    {
    public:
        //! @brief 要求の猶予が残り、spent と wasSlamming が偽で locomotion が真かを返す
        //! @param[in] locomotion 立ち・歩き・落ち・反動のどれかの状態か
        //! @return 発動する場合 true、それ以外の場合は false
        [[nodiscard]] static bool Judge(float bufferRemaining, bool spent, bool wasSlamming, bool locomotion) noexcept
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
            return !grounded && !(verticalVelocity > 0.0f) && !wasSlamming;
        }

        //! @brief 箱の上端が手の高さの帯に入り、手が箱の水平の範囲の中にあるかを返す
        //! @param[in] probe 手の位置。世界座標
        //! @param[in] box 調べる箱
        //! @param[in] belowHand 手より下に許す上端の深さ。単位は m
        //! @param[in] aboveHand 手より上に許す上端の高さ。単位は m
        //! @return 入っている場合 true、それ以外の場合は false
        [[nodiscard]] static bool InBand(const NS::Core::Vector3& probe,
                                         const NS::Core::AABB& box,
                                         float belowHand,
                                         float aboveHand) noexcept
        {
            const float top = box.Center.y + box.Extents.y;
            return !(top < probe.y - belowHand || top > probe.y + aboveHand || probe.x < box.Center.x - box.Extents.x ||
                     probe.x > box.Center.x + box.Extents.x || probe.z < box.Center.z - box.Extents.z ||
                     probe.z > box.Center.z + box.Extents.z);
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
        //! 接地していて歩いていない場合 true、それ以外の場合は false
        [[nodiscard]] static bool Judge(bool grounded, bool walking) noexcept { return grounded && !walking; }
    };

    //! 切り返しのブレーキの判定
    class PlayerJudgeBrake
    {
    public:
        //! @brief 入力があり、入力の水平の向きへの水平の速度の成分が threshold 未満かを返す
        //! @return ブレーキを掛ける場合 true、それ以外の場合は false。入力の向きが水平に無い時は false
        [[nodiscard]] static bool Judge(bool hasInput,
                                        const NS::Core::Vector3& desiredDirection,
                                        const NS::Core::Vector3& lateralVelocity,
                                        float threshold) noexcept
        {
            NS::Core::Vector3 direction{};
            if (!hasInput || !NS::Core::TryNormalizeHorizontal(desiredDirection, direction))
            {
                return false;
            }
            return direction.x * lateralVelocity.x + direction.z * lateralVelocity.z < threshold;
        }
    };

    //! 着地の判定
    class PlayerJudgeLand
    {
    public:
        //! @brief 接地していて上昇していない場合 true、それ以外の場合は false
        //! @details 反動の明けのフレームは止める前の接地の印が残ったまま上向きの速度が入る。
        //! 接地だけを見ると宙へ出る前に立ちへ移る
        [[nodiscard]] static bool Judge(bool grounded, float verticalVelocity) noexcept
        {
            return grounded && !(verticalVelocity > 0.0f);
        }
    };

    //! 落下の判定
    class PlayerJudgeFall
    {
    public:
        //! 接地を外れている場合 true、それ以外の場合は false
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
        [[nodiscard]] static bool Judge(const NS::Core::Vector3& lateralVelocity) noexcept
        {
            return lateralVelocity.x == 0.0f && lateralVelocity.z == 0.0f;
        }
    };

    //! 縁をよじ登るかの判定
    class PlayerJudgeClimbLedge
    {
    public:
        //! 前入力が出ている場合 true、それ以外の場合は false
        [[nodiscard]] static bool Judge(float climbForward) noexcept { return climbForward > 0.0f; }
    };

    //! 縁を手放すかの判定
    class PlayerJudgeDropLedge
    {
    public:
        //! 手放しのボタンが押された場合 true、それ以外の場合は false
        [[nodiscard]] static bool Judge(bool releasePressed) noexcept { return releasePressed; }
    };

    //! 通常移動の状態かの判定
    class PlayerJudgeLocomotion
    {
    public:
        //! 立ち・走り・落下・反動のどれかの状態の場合 true、それ以外の場合は false
        [[nodiscard]] static bool Judge(bool idle, bool walk, bool fall, bool rebound) noexcept
        {
            return idle || walk || fall || rebound;
        }
    };
} // namespace NS::Game::Player
