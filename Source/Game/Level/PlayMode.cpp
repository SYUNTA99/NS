#include "Game/Level/PlayMode.h"

#include "Game/Level/LevelData.h"
#include "Game/Level/PlayState.h"

#include <algorithm>
#include <variant>

namespace NS::Game::Level
{
    namespace
    {
        // PickupComponent の "Pickup Kind" を返す。 PickupComponent が無ければ -1、 フィールド欠損は 0 (コイン既定)
        int PickupKindOf(const ObjectInstance& object) noexcept
        {
            for (const auto& component : object.components)
            {
                if (component.typeName != "PickupComponent")
                    continue;
                for (const auto& field : component.fields)
                    if (field.name == "Pickup Kind" && std::holds_alternative<int>(field.value))
                        return std::get<int>(field.value);
                return 0;
            }
            return -1;
        }

        // 拾得種別は PickupComponent だけで決まる。 読込時移行でどの拾得物も PickupComponent を持つ
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
        // cell 中心に置くと直下ブロックに 0.4m めり込み swept が toi=0 を返し続けて操作不能になる
        // capsule 底端を spawn セル底面に乗せ、さらに 1cm 浮かせて浮動小数誤差の安全マージンを取る
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

} // namespace NS::Game::Level
