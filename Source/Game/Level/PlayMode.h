#pragma once

/// @file PlayMode.h
/// @brief NS::Game::Level::PlayMode — Play 中のゲームルール bookkeeping を Enter/Tick/Exit で担う
///
/// @details 物理すなわち移動 / 重力 / 衝突 / 掴まりは Player の CharacterMovementComponent が担う
/// PlayMode は Play 開始時のプレイヤー実体位置への配置 + counter リセットを Enter で行い、 毎フレームの
/// 落下死 / coin / goal 取得判定を Tick で受け持つ
/// `const NS::Scene::SceneData&` を Tick 受取に強制することで、 PlayMode 経路では NS::Scene::SceneData を
/// 書き換えられないことを compile-time に保証する。 player 位置は LevelPlayScene が
/// SSOT である Transform から `PlayState::playerPosition` にミラーした値を読む
///
/// 落下死: `playerPosition.y < kFallDeathThreshold` で deathTriggered
/// coin/goal 接触: kPickupRadius を使う中心間距離の単純比較

namespace NS::Scene
{
    struct SceneData;
} // namespace NS::Scene

namespace NS::Game::Level
{
    struct PlayState;

    /// Play 中の simulation。 EditorMode と value member で並列保有、 mode flag flip で active を切替える
    class PlayMode
    {
    public:
        /// 落下死判定の y 閾値。 これより低くなったら deathTriggered
        static constexpr float kFallDeathThreshold = -50.0f;
        /// coin / power goal を「取れた」 とみなす player 中心からのメートル距離
        static constexpr float kPickupRadius = 0.9f;

        PlayMode() noexcept;
        ~PlayMode() noexcept;

        PlayMode(const PlayMode&) = delete;
        PlayMode& operator=(const PlayMode&) = delete;
        PlayMode(PlayMode&&) = delete;
        PlayMode& operator=(PlayMode&&) = delete;

        void SetActive(bool active) noexcept { m_active = active; }
        [[nodiscard]] bool IsActive() const noexcept { return m_active; }

        /// Edit→Play 遷移。 player をプレイヤー実体の位置へ再配置し velocity / flag を全リセット
        void Enter(const NS::Scene::SceneData& level, PlayState& play) noexcept;

        /// fixed step Tick。 落下死 / coin / power goal 接触判定。 paused 中は何もしない
        void Tick(const NS::Scene::SceneData& level, PlayState& play, float dt) noexcept;

        /// Play→Edit 遷移。 paused / clearTriggered / deathTriggered を false にリセット
        void Exit(PlayState& play) noexcept;

    private:
        bool m_active = false;

        /// 二重カウント防止用。 NS::Scene::SceneData の block は Play 中は不変なので削除しない
        std::vector<std::size_t> m_collectedCoinIndices;
    };

    /// 接触ダメージを 1 与える。 health 下限は 0、 0 到達で deathTriggered を立てる
    void ApplyContactDamage(PlayState& play) noexcept;
} // namespace NS::Game::Level
