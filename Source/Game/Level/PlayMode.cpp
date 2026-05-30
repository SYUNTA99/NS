#include "Game/Level/PlayMode.h"

#include "Game/Editor/BlockRegistry.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/PlayState.h"

#include <algorithm>

namespace NS::Game::Level
{

    PlayMode::PlayMode() noexcept = default;
    PlayMode::~PlayMode() noexcept = default;

    void PlayMode::Enter(const LevelData& level, PlayState& play) noexcept
    {
        // capsule 縦総長 = (halfHeight + radius) × 2 = 1.8m で 1m cell より大きい。
        // cell 中心に置くと直下ブロックに 0.4m 深くめり込み、 controller の swept 衝突が
        // toi=0 を返し続けて horizontal motion も止まる (= 操作不能) ので、 capsule の底端を
        // spawn セルの底面 (= 直下にブロックがあればその上面) に乗せる位置に center を置く。
        //
        // さらに 1cm 浮かせて、 浮動小数誤差で feet がブロック上面と完全一致した時にも
        // 1tick 目の gravity が確実に着地させる安全マージンを取る。
        constexpr float kCellHalfExtent = 0.5f;
        constexpr float kSpawnLiftEpsilon = 0.01f;
        const float playerCenterY = static_cast<float>(level.spawnY) - kCellHalfExtent + kPlayerCapsuleHalfHeight +
                                    kPlayerCapsuleRadius + kSpawnLiftEpsilon;
        play.playerPosition = {static_cast<float>(level.spawnX), playerCenterY, static_cast<float>(level.spawnZ)};
        play.playerVelocity = {0.0f, 0.0f, 0.0f};
        play.coinCount = 0;
        play.remainingSeconds = static_cast<float>(level.timeLimitSeconds);
        play.paused = false;
        play.clearTriggered = false;
        play.deathTriggered = false;
        // HUD / 回復アイテムが入るまで、 Play 開始 / respawn 毎に最大値へ戻す placeholder。
        play.playerHealth = 8;

        m_collectedCoinIndices.clear();
    }

    void PlayMode::Tick(const LevelData& level, PlayState& play, float dt) noexcept
    {
        if (play.paused)
            return;
        if (dt <= 0.0f)
            return;

        // 物理 (移動 / 重力 / 衝突) は CharacterMovementComponent が担う。 ここは Transform から
        // ミラーされた play.playerPosition を読んでゲームルールだけを評価する。
        if (play.playerPosition.y < kFallDeathThreshold)
            play.deathTriggered = true;

        const float pickupSq = kPickupRadius * kPickupRadius;
        for (std::size_t i = 0; i < level.blocks.size(); ++i)
        {
            const auto& entry = level.blocks[i];
            const bool isCoin = (entry.blockId == NS::Game::Editor::kBlockIdCoin);
            const bool isStar = (entry.blockId == NS::Game::Editor::kBlockIdPowerStar);
            if (!isCoin && !isStar)
                continue;

            const float dx = static_cast<float>(entry.x) - play.playerPosition.x;
            const float dy = static_cast<float>(entry.y) - play.playerPosition.y;
            const float dz = static_cast<float>(entry.z) - play.playerPosition.z;
            if (dx * dx + dy * dy + dz * dz >= pickupSq)
                continue;

            if (isCoin)
            {
                const auto found = std::find(m_collectedCoinIndices.begin(), m_collectedCoinIndices.end(), i);
                if (found == m_collectedCoinIndices.end())
                {
                    m_collectedCoinIndices.push_back(i);
                    play.coinCount += 1;
                }
            }
            else
            {
                play.clearTriggered = true;
            }
        }
    }

    void PlayMode::Exit(PlayState& play) noexcept
    {
        // Quit-to-Edit 中の paused 残留や、 clear/death の flag を持ち越さないようリセット。
        // 次の EnterPlay は Enter() でも上書きされるが、 Exit 直後に PlayState を観測する
        // EditorLayer 等のために整える。
        play.paused = false;
        play.clearTriggered = false;
        play.deathTriggered = false;
        m_collectedCoinIndices.clear();
    }

} // namespace NS::Game::Level
