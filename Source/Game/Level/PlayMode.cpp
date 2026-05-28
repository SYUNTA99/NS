#include "Game/Level/PlayMode.h"

#include "Game/Editor/BlockRegistry.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/PlayState.h"

#include <algorithm>
#include <cmath>

namespace NS::Game::Level
{

    PlayMode::PlayMode() noexcept = default;
    PlayMode::~PlayMode() noexcept = default;

    void PlayMode::SetDesiredMove(const NS::Core::Vector3& worldDir, float speedScale01) noexcept
    {
        m_desiredDir = {worldDir.x, 0.0f, worldDir.z};
        m_desiredSpeed = std::clamp(speedScale01, 0.0f, 1.0f) * kMoveSpeed;
    }

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

        m_collectedCoinIndices.clear();
        m_desiredDir = {0.0f, 0.0f, 0.0f};
        m_desiredSpeed = 0.0f;
        m_jumpPressed = false;
        m_grounded = false;
    }

    void PlayMode::Tick(const LevelData& level, PlayState& play, float dt) noexcept
    {
        if (play.paused)
            return;
        if (dt <= 0.0f)
            return;

        // 水平速度は入力で即時セット (Mario 風の直接操作、 加速感は + で再検討)。
        play.playerVelocity.x = m_desiredDir.x * m_desiredSpeed;
        play.playerVelocity.z = m_desiredDir.z * m_desiredSpeed;

        // ジャンプ: 接地 frame で edge 入力が来ていたら 1 度だけ初速を与える。
        if (m_jumpPressed && m_grounded)
            play.playerVelocity.y = kJumpImpulse;
        m_jumpPressed = false;

        play.playerVelocity.y += kGravity * dt;

        // Solid block だけ collision world を組む。 coin / power star は通過可能。
        std::vector<NS::Core::AABB> world;
        world.reserve(level.blocks.size());
        for (const auto& entry : level.blocks)
        {
            if (entry.blockId != NS::Game::Editor::kBlockIdSolid)
                continue;
            const NS::Core::Vector3 center{
                static_cast<float>(entry.x), static_cast<float>(entry.y), static_cast<float>(entry.z)};
            world.emplace_back(center, NS::Core::Vector3{0.5f, 0.5f, 0.5f});
        }

        NS::Physics::CharacterControllerInput input{};
        input.position = play.playerPosition;
        input.velocity = play.playerVelocity;
        input.dt = dt;
        input.capsuleRadius = kPlayerCapsuleRadius;
        input.capsuleHalfHeight = kPlayerCapsuleHalfHeight;
        input.world = std::span<const NS::Core::AABB>(world);
        const auto result = m_controller.Update(input);
        play.playerPosition = result.position;
        play.playerVelocity = result.velocity;
        m_grounded = result.grounded;

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
        m_desiredDir = {0.0f, 0.0f, 0.0f};
        m_desiredSpeed = 0.0f;
        m_jumpPressed = false;
        m_grounded = false;
    }

} // namespace NS::Game::Level
