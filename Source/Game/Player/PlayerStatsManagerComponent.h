#pragma once

#include "Game/Entity/EntityStatsManagerComponent.h"
#include "Game/Player/PlayerStats.h"
#include "Runtime/Object/Reflection/Reflection.h"

#include <array>
#include <cstddef>

namespace NS::Game::Player
{
    //! @brief 自機の調整値の組を持つ Component
    //! @details 組は 1 つで始める。Inspector の欄は現在の組を直接編集する
    //! 欄の表示名は CharacterMovementComponent と 1 文字も違えない。変えると保存済みシーンの値が既定へ化ける
    //! 依存: NS::Game::Entity::EntityStatsManagerComponent, PlayerStats
    class PlayerStatsManagerComponent : public NS::Game::Entity::EntityStatsManagerComponent
    {
    public:
        [[nodiscard]] const PlayerStats& Current() const noexcept { return m_sets[CurrentIndex()]; } //!< 現在の組
        //! 現在の組の書き換え口。値の検査は各 setter 側にあるので、ここを直に触ると非有限値を通す
        [[nodiscard]] PlayerStats& CurrentMutable() noexcept { return m_sets[CurrentIndex()]; }

        [[nodiscard]] std::size_t StatsCount() const noexcept override { return m_sets.size(); }

        // setter は非有限値を書き込まない。重力や時定数へ入ると位置まで NaN が伝わる
        [[nodiscard]] float JumpImpulse() const noexcept { return Current().jumpImpulse; }
        void SetJumpImpulse(float value) noexcept;

        [[nodiscard]] float GravityUp() const noexcept { return Current().gravityUp; }
        void SetGravityUp(float value) noexcept;

        [[nodiscard]] float GravityDown() const noexcept { return Current().gravityDown; }
        void SetGravityDown(float value) noexcept;

        [[nodiscard]] float ApexHangVy() const noexcept { return Current().apexHangVy; }
        void SetApexHangVy(float value) noexcept;

        [[nodiscard]] float ApexHangScale() const noexcept { return Current().apexHangScale; }
        void SetApexHangScale(float value) noexcept;

        [[nodiscard]] float JumpReleaseScale() const noexcept { return Current().jumpReleaseScale; }
        void SetJumpReleaseScale(float value) noexcept;

        [[nodiscard]] float CoyoteTime() const noexcept { return Current().coyoteTime; }
        void SetCoyoteTime(float value) noexcept;

        [[nodiscard]] float JumpBufferTime() const noexcept { return Current().jumpBufferTime; }
        void SetJumpBufferTime(float value) noexcept;

        [[nodiscard]] float WalkSpeed() const noexcept { return Current().walkSpeed; }
        void SetWalkSpeed(float value) noexcept;

        [[nodiscard]] float AccelTau() const noexcept { return Current().accelTau; }
        void SetAccelTau(float value) noexcept;

        [[nodiscard]] float DecelTau() const noexcept { return Current().decelTau; }
        void SetDecelTau(float value) noexcept;

        [[nodiscard]] float StickDeadzone() const noexcept { return Current().stickDeadzone; }
        void SetStickDeadzone(float value) noexcept;

        [[nodiscard]] float BodySlamSpeed() const noexcept { return Current().bodySlamSpeed; }
        void SetBodySlamSpeed(float value) noexcept;

        [[nodiscard]] float BodySlamDistance() const noexcept { return Current().bodySlamDistance; }
        void SetBodySlamDistance(float value) noexcept;

        [[nodiscard]] float TapSlamSpeed() const noexcept { return Current().tapSlamSpeed; }
        void SetTapSlamSpeed(float value) noexcept;

        [[nodiscard]] float TapSlamUpSpeed() const noexcept { return Current().tapSlamUpSpeed; }
        void SetTapSlamUpSpeed(float value) noexcept;

        [[nodiscard]] float TapSlamDistance() const noexcept { return Current().tapSlamDistance; }
        void SetTapSlamDistance(float value) noexcept;

        // 欄は登録される具象型に置く。リフレクションの直列化は自分の型の欄だけを回り、基底の鎖はたどらない
        NS_REFLECT_BEGIN(PlayerStatsManagerComponent, NS::Game::Entity::EntityStatsManagerComponent)
        NS_REFLECT_ACCESSOR(float, "ジャンプ初速", JumpImpulse(), SetJumpImpulse)
        NS_REFLECT_ACCESSOR(float, "上昇重力", GravityUp(), SetGravityUp)
        NS_REFLECT_ACCESSOR(float, "下降重力", GravityDown(), SetGravityDown)
        NS_REFLECT_ACCESSOR(float, "頂点滞空 Vy", ApexHangVy(), SetApexHangVy)
        NS_REFLECT_ACCESSOR(float, "頂点滞空倍率", ApexHangScale(), SetApexHangScale)
        NS_REFLECT_ACCESSOR(float, "ジャンプ離し倍率", JumpReleaseScale(), SetJumpReleaseScale)
        NS_REFLECT_ACCESSOR(float, "コヨーテ時間", CoyoteTime(), SetCoyoteTime)
        NS_REFLECT_ACCESSOR(float, "先行入力時間", JumpBufferTime(), SetJumpBufferTime)
        NS_REFLECT_ACCESSOR(float, "歩き速度", WalkSpeed(), SetWalkSpeed)
        NS_REFLECT_ACCESSOR(float, "加速時定数", AccelTau(), SetAccelTau)
        NS_REFLECT_ACCESSOR(float, "減速時定数", DecelTau(), SetDecelTau)
        NS_REFLECT_ACCESSOR(float, "スティック遊び", StickDeadzone(), SetStickDeadzone)
        NS_REFLECT_ACCESSOR(float, "突進速度", BodySlamSpeed(), SetBodySlamSpeed)
        NS_REFLECT_ACCESSOR(float, "突進距離", BodySlamDistance(), SetBodySlamDistance)
        NS_REFLECT_ACCESSOR(float, "タップ初速", TapSlamSpeed(), SetTapSlamSpeed)
        NS_REFLECT_ACCESSOR(float, "タップの上向き初速", TapSlamUpSpeed(), SetTapSlamUpSpeed)
        NS_REFLECT_ACCESSOR(float, "タップ距離", TapSlamDistance(), SetTapSlamDistance)
        NS_REFLECT_END()

    private:
        std::array<PlayerStats, 1> m_sets{}; // 調整値の組。長さを型で固定し、空になる場合を作らない
    };
} // namespace NS::Game::Player
