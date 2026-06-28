#include "Game/Level/PlayMode.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/PlayState.h"

#include <algorithm>

namespace NS::Game::Level
{
    namespace
    {
        // 拾得種別は PickupComponent だけで決まる。 読込時移行でどの拾得物も PickupComponent を持つ
        // 種別判定は LevelData の共有 PickupKindOf に一本化し、 配置物の表示・固形判定と同じ契約を読む
        bool ObjectIsCoin(const ObjectInstance& object) noexcept
        {
            return PickupKindOf(object) == 0;
        }

        bool ObjectIsStar(const ObjectInstance& object) noexcept
        {
            return PickupKindOf(object) == 1;
        }
    } // namespace

    PlayMode::PlayMode() noexcept = default;
    PlayMode::~PlayMode() noexcept = default;

    void PlayMode::Enter(const LevelData& level, PlayState& play) noexcept
    {
        // spawn はエディタで配置した実プレイヤーの capsule 中心 world 位置そのもの
        // 床乗せの補正は配置時に決まっているのでここでは持たず、 焼かれた位置へそのまま置く
        play.playerPosition = {level.spawnX, level.spawnY, level.spawnZ};
        play.playerVelocity = {0.0f, 0.0f, 0.0f};
        play.coinCount = 0;
        play.remainingSeconds = static_cast<float>(level.timeLimitSeconds);
        play.paused = false;
        play.clearTriggered = false;
        play.deathTriggered = false;
        play.playerHealth = 8;

        m_collectedCoinIndices.clear();
    }

    void PlayMode::Tick(const LevelData& level, PlayState& play, float dt) noexcept
    {
        if (play.paused)
            return;
        if (dt <= 0.0f)
            return;

        // 物理すなわち移動 / 重力 / 衝突は CharacterMovementComponent が担う。 ここは Transform から
        // ミラーされた play.playerPosition を読んでゲームルールだけを評価する
        if (play.playerPosition.y < kFallDeathThreshold)
            play.deathTriggered = true;

        const float pickupSq = kPickupRadius * kPickupRadius;
        for (std::size_t i = 0; i < level.objects.size(); ++i)
        {
            const auto& entry = level.objects[i];
            const bool isCoin = ObjectIsCoin(entry);
            const bool isStar = ObjectIsStar(entry);
            if (!isCoin && !isStar)
                continue;

            const float dx = entry.positionX - play.playerPosition.x;
            const float dy = entry.positionY - play.playerPosition.y;
            const float dz = entry.positionZ - play.playerPosition.z;
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
        // Enter() でも上書きされるが、 Exit 直後に PlayState を観測する側のために整える
        play.paused = false;
        play.clearTriggered = false;
        play.deathTriggered = false;
        m_collectedCoinIndices.clear();
    }

    void ApplyContactDamage(PlayState& play) noexcept
    {
        if (play.playerHealth <= 0)
            return;

        --play.playerHealth;
        if (play.playerHealth <= 0)
        {
            play.playerHealth = 0;
            play.deathTriggered = true;
            NS_LOG_INFO(::NS::Core::LogCat::Game, "ハザード接触で死亡 placeholder、 将来 HUD / 回復 / 演出に置換予定");
        }
    }

} // namespace NS::Game::Level
