#pragma once

/// @file PlayMode.h
/// @brief NS::Game::Level::PlayMode — Play 中の simulation lifecycle (Enter/Tick/Exit)。
///
/// @details `const LevelData&` を Tick 受取に強制することで、 PlayMode 経路では
/// LevelData を書き換えられないことを compile-time に保証する。
/// PlayState のみ mutable で、 player position / velocity / coin counter / clear /
/// death の flag を持つ。
///
/// PlayState を player 状態の Single Source of Truth とし、 PlayMode が
/// CharacterController を内包する simplified physics で直接 mutate する。
///  既存の Player::Movement() / Player::InputComp() は  PlayMode 内では
/// 使用せず、 LevelEditorScene 側で `Player.Transform.SetPosition(PlayState.playerPosition)` の
/// 一方向同期 (visual mirror) を行う。 + で Player class 統合を再検討。
///
/// 落下死 (): `playerPosition.y < kFallDeathThreshold` で deathTriggered。
/// coin/star 接触: 単純な距離比較 (vertical slice の簡易版、 + で AABB-vs-AABB に置換予定)。

#include "Framework/Core/Math.h"
#include "Framework/Physics/CharacterController.h"

#include <cstddef>
#include <vector>

namespace NS::Game::Level
{
    struct LevelData;
    struct PlayState;

    /// Play 中の simulation。 EditorMode と value member で並列保有、 mode flag flip で active を切替える。
    class PlayMode
    {
    public:
        /// 落下死判定の y 閾値 (これより低くなったら deathTriggered)。
        static constexpr float kFallDeathThreshold = -50.0f;
        /// 重力 (y 方向)。  CharacterMovementComponent::m_gravityUp と手動同期。
        static constexpr float kGravity = -25.0f;
        /// player capsule 半径。  CharacterMovementComponent::m_capsuleRadius と手動同期。
        static constexpr float kPlayerCapsuleRadius = 0.4f;
        /// player capsule の半身長。  CharacterMovementComponent::m_capsuleHalfHeight と手動同期。
        static constexpr float kPlayerCapsuleHalfHeight = 0.5f;
        /// coin / power star を「取れた」 とみなす player 中心からの距離 (m)。
        static constexpr float kPickupRadius = 0.9f;
        /// 立ち上がりエッジでジャンプを発動した時の初速。
        static constexpr float kJumpImpulse = 12.0f;
        /// 入力で達成できる水平最大速度 (m/s)、 + で walk/run 切替予定。
        static constexpr float kMoveSpeed = 6.0f;

        PlayMode() noexcept;
        ~PlayMode() noexcept;

        PlayMode(const PlayMode&) = delete;
        PlayMode& operator=(const PlayMode&) = delete;
        PlayMode(PlayMode&&) = delete;
        PlayMode& operator=(PlayMode&&) = delete;

        void SetActive(bool active) noexcept { m_active = active; }
        [[nodiscard]] bool IsActive() const noexcept { return m_active; }

        /// Edit→Play 遷移時に呼ぶ。 player を spawn 位置 (spawn セル中心の床上) に再配置、
        /// velocity ゼロ、 coin / death / clear / paused flag をリセットする。
        void Enter(const LevelData& level, PlayState& play) noexcept;

        /// fixed step Tick。 const& 受取で書込禁止を compile-time 保証。
        /// gravity と入力した desired velocity を踏まえて CharacterController で物理更新し、
        /// 落下死 / coin / power star 接触判定を行う。 paused == true の間は no-op。
        void Tick(const LevelData& level, PlayState& play, float dt) noexcept;

        /// Play→Edit 遷移時に呼ぶ。 次回 Enter で操作不能にならないよう、
        /// paused / clearTriggered / deathTriggered を全て false に戻す。
        void Exit(PlayState& play) noexcept;

        /// 水平方向の希望移動 (world 座標、 y は無視)。 入力 polling した側 (LevelEditorScene)
        /// が毎フレーム Tick 前に呼ぶ。 dir が 0 で停止扱い。
        void SetDesiredMove(const NS::Core::Vector3& worldDir, float speedScale01) noexcept;
        /// ジャンプの押下立ち上がり (edge)。 接地中の Tick で 1 度だけジャンプに変換される。
        void SetJumpPressed() noexcept { m_jumpPressed = true; }

        [[nodiscard]] bool IsGrounded() const noexcept { return m_grounded; }

    private:
        bool m_active = false;
        bool m_grounded = false;
        bool m_jumpPressed = false;

        NS::Core::Vector3 m_desiredDir{0.0f, 0.0f, 0.0f};
        float m_desiredSpeed = 0.0f;

        NS::Physics::CharacterController m_controller;

        /// 二重カウント防止のため取得済 coin の index を保持する。 LevelData の coin block
        /// 自体は削除しない ( 整合: Play 中の LevelData は不変)。
        std::vector<std::size_t> m_collectedCoinIndices;
    };
} // namespace NS::Game::Level
