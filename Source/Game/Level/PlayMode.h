#pragma once

/// @file PlayMode.h
/// @brief NS::Game::Level::PlayMode — Play 中のゲームルール bookkeeping (Enter/Tick/Exit)
///
/// @details 物理 (移動 / 重力 / 衝突 / 掴まり) は Player の CharacterMovementComponent が担う
/// PlayMode は Play 開始時の spawn 配置 + counter リセット (Enter) と、 毎フレームの
/// 落下死 / coin / star 取得判定 (Tick) のみを受け持つ
/// `const LevelData&` を Tick 受取に強制することで、 PlayMode 経路では LevelData を
/// 書き換えられないことを compile-time に保証する。 player 位置は LevelEditorScene が
/// Transform (SSOT) から `PlayState::playerPosition` にミラーした値を読む
///
/// 落下死: `playerPosition.y < kFallDeathThreshold` で deathTriggered
/// coin/star 接触: 中心間距離の単純比較 (kPickupRadius)

#include "Framework/Math/Math.h"

#include <cstddef>
#include <vector>

namespace NS::Game::Level
{
    struct LevelData;
    struct PlayState;

    /// Play 中の simulation。 EditorMode と value member で並列保有、 mode flag flip で active を切替える
    class PlayMode
    {
    public:
        /// 落下死判定の y 閾値 (これより低くなったら deathTriggered)
        static constexpr float kFallDeathThreshold = -50.0f;
        /// spawn 配置に使う player capsule の半径 / 半身長 (CharacterMovementComponent の既定値と一致)
        static constexpr float kPlayerCapsuleRadius = 0.4f;
        static constexpr float kPlayerCapsuleHalfHeight = 0.5f;
        /// coin / power star を「取れた」 とみなす player 中心からの距離 (m)
        static constexpr float kPickupRadius = 0.9f;

        PlayMode() noexcept;
        ~PlayMode() noexcept;

        PlayMode(const PlayMode&) = delete;
        PlayMode& operator=(const PlayMode&) = delete;
        PlayMode(PlayMode&&) = delete;
        PlayMode& operator=(PlayMode&&) = delete;

        void SetActive(bool active) noexcept { m_active = active; }
        [[nodiscard]] bool IsActive() const noexcept { return m_active; }

        /// Edit→Play 遷移。 player を spawn セル床上に再配置し velocity / flag を全リセット
        void Enter(const LevelData& level, PlayState& play) noexcept;

        /// fixed step Tick。 落下死 / coin / power star 接触判定。 paused 中は何もしない
        void Tick(const LevelData& level, PlayState& play, float dt) noexcept;

        /// Play→Edit 遷移。 paused / clearTriggered / deathTriggered を false にリセット
        void Exit(PlayState& play) noexcept;

    private:
        bool m_active = false;

        /// 二重カウント防止用。 LevelData の block は削除しない (Play 中は不変)
        std::vector<std::size_t> m_collectedCoinIndices;
    };
} // namespace NS::Game::Level
