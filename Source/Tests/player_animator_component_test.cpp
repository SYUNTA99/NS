#include <Game/Player/PlayerAnimatorComponent.h>
#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerStateManagerComponent.h>
#include <Game/Player/States/LedgeHangingPlayerState.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Graphics/Animation.h>
#include <Runtime/Object/Components/SkeletalAnimationComponent.h>
#include <Runtime/Object/GameObject.h>
#include <gtest/gtest.h>

#include "tuning_field_access.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using NS::Core::Vector3;
    using NS::Game::Player::PlayerAnimatorComponent;
    using NS::Game::Player::PlayerComponent;
    using NS::Game::Player::PlayerStateManagerComponent;
    using NS::Graphics::AnimationClip;
    using NS::Object::GameObject;
    using NS::Object::SkeletalAnimationComponent;

    constexpr float k_FixedDt = 1.0f / 60.0f;

    // 添字で確かめるので並べた順が意味を持つ
    constexpr std::size_t k_Idle = 0;
    constexpr std::size_t k_Walk = 1;
    constexpr std::size_t k_Run = 2;

    AnimationClip MakeClip(std::string name)
    {
        AnimationClip clip;
        clip.name = std::move(name);
        clip.duration = 10.0f;
        return clip;
    }

    // クリップの実体は試し側が持つ。SkeletalAnimationComponent は番地を控えるだけ
    struct Rig
    {
        GameObject owner;
        std::vector<AnimationClip> clips;
        PlayerComponent* player = nullptr;
        PlayerStateManagerComponent* states = nullptr;
        SkeletalAnimationComponent* anim = nullptr;
        PlayerAnimatorComponent* animator = nullptr;

        Rig()
        {
            clips.push_back(MakeClip("idle"));
            clips.push_back(MakeClip("walk"));
            clips.push_back(MakeClip("run"));

            states = owner.AddComponent<PlayerStateManagerComponent>();
            player = owner.AddComponent<PlayerComponent>();
            anim = owner.AddComponent<SkeletalAnimationComponent>();
            animator = owner.AddComponent<PlayerAnimatorComponent>();

            anim->AddClips(clips);
            player->OnStart();
            states->OnStart();
            animator->OnStart();
            states->EnsureBuilt(*player);
        }

        void Ground(float lateralSpeed)
        {
            player->SetGrounded(true);
            player->SetVelocity(Vector3{lateralSpeed, 0.0f, 0.0f});
        }
    };
} // namespace

TEST(PlayerAnimatorTest, StandingStillPicksIdle)
{
    Rig rig;
    rig.Ground(0.0f);

    rig.animator->OnUpdate();

    EXPECT_EQ(rig.anim->CurrentClip(), k_Idle);
}

TEST(PlayerAnimatorTest, SlowGroundSpeedPicksWalk)
{
    Rig rig;
    rig.Ground(rig.player->MaxSpeed() * 0.2f);

    rig.animator->OnUpdate();

    EXPECT_EQ(rig.anim->CurrentClip(), k_Walk);
}

TEST(PlayerAnimatorTest, FastGroundSpeedPicksRun)
{
    Rig rig;
    rig.Ground(rig.player->MaxSpeed() * 0.9f);

    rig.animator->OnUpdate();

    EXPECT_EQ(rig.anim->CurrentClip(), k_Run);
}

// 毎フレーム選び直すと再生時刻が 0 へ戻り、絵が 1 コマ目で止まる
TEST(PlayerAnimatorTest, HoldingTheSameClipKeepsPlaying)
{
    Rig rig;
    rig.Ground(rig.player->MaxSpeed() * 0.9f);

    rig.animator->OnUpdate();
    rig.anim->OnUpdate();
    const float first = rig.anim->Time();
    rig.animator->OnUpdate();
    rig.anim->OnUpdate();

    EXPECT_GT(first, 0.0f);
    EXPECT_GT(rig.anim->Time(), first);
}

TEST(PlayerAnimatorTest, PlaybackSpeedFollowsTheGroundSpeed)
{
    Rig fast;
    fast.Ground(fast.player->MaxSpeed());
    fast.animator->OnUpdate();
    fast.anim->OnUpdate();

    Rig slow;
    slow.Ground(slow.player->MaxSpeed() * 0.5f);
    slow.animator->OnUpdate();
    slow.anim->OnUpdate();

    EXPECT_GT(fast.anim->Time(), slow.anim->Time());
}

TEST(PlayerAnimatorTest, PlaybackSpeedStopsAtTheFloor)
{
    Rig rig;
    rig.Ground(rig.player->MaxSpeed() * 0.01f);

    rig.animator->OnUpdate();
    rig.anim->OnUpdate();

    // 0.5 は m_minPlaybackSpeed の既定。既定を触るとこの試しが落ちる
    EXPECT_NEAR(rig.anim->Time(), k_FixedDt * 0.5f, 1e-4f);
}

// 走行速度はリフレクションの欄なので Inspector から 0 を打てる。割り算を素通りさせると
// 再生速度が無限大になり、長さで折り返す時に再生時刻が非数で固まる
TEST(PlayerAnimatorTest, ZeroRunSpeedKeepsPlaybackAtNormal)
{
    Rig rig;
    NsTest::WriteTuningField(*rig.player, "走行速度", 0.0f);
    rig.Ground(4.0f);

    rig.animator->OnUpdate();
    rig.anim->OnUpdate();

    EXPECT_NEAR(rig.anim->Time(), k_FixedDt, 1e-4f);
}

TEST(PlayerAnimatorTest, MissingAirClipFallsBackToIdle)
{
    Rig rig;
    rig.Ground(rig.player->MaxSpeed() * 0.9f);
    rig.animator->OnUpdate();
    ASSERT_EQ(rig.anim->CurrentClip(), k_Run);

    rig.player->SetGrounded(false);
    rig.player->SetVerticalVelocity(-5.0f);
    rig.animator->OnUpdate();

    EXPECT_EQ(rig.anim->CurrentClip(), k_Idle);
}

TEST(PlayerAnimatorTest, MissingLedgeClipFallsBackToIdle)
{
    Rig rig;
    rig.Ground(rig.player->MaxSpeed() * 0.9f);
    rig.animator->OnUpdate();
    ASSERT_EQ(rig.anim->CurrentClip(), k_Run);

    rig.states->Change<NS::Game::Player::LedgeHangingPlayerState>();
    rig.animator->OnUpdate();

    EXPECT_EQ(rig.anim->CurrentClip(), k_Idle);
}

TEST(PlayerAnimatorTest, UnknownClipNameFallsBackToIdle)
{
    Rig rig;
    rig.animator->SetRunClip("無い名前");
    rig.Ground(rig.player->MaxSpeed() * 0.9f);

    rig.animator->OnUpdate();

    EXPECT_EQ(rig.anim->CurrentClip(), k_Idle);
}

TEST(PlayerAnimatorTest, WithoutASkeletalAnimationNothingHappens)
{
    GameObject owner;
    auto* player = owner.AddComponent<PlayerComponent>();
    auto* animator = owner.AddComponent<PlayerAnimatorComponent>();
    player->OnStart();
    animator->OnStart();

    animator->OnUpdate();

    SUCCEED();
}
