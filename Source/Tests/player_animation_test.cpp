#include "Game/Player.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/States/LedgeHangingPlayerState.h"
#include "NSlib/Graphics/Animation.h"
#include "NSlib/Object/SubObjects/Animation.h"
#include "NSlib/Object/SubObjects/Body.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Windows/Clock.h"

#include <gtest/gtest.h>

#include <vector>

TEST(PlayerAnimation, PlayerSelectsMovementClipWithoutRestartingItsTime)
{
    const std::vector<NS::Gfx::AnimationClip> clips = {
        {.name = "idle", .duration = 10.0f}, {.name = "run", .duration = 10.0f}, {.name = "walk", .duration = 10.0f}};
    Player player;
    player.EnsureInit();
    NS::Obj::Animation* animation = NS::Obj::Cast<NS::Obj::Animation>(player.CreateSubObj("Animation"));
    animation->AddClips(clips);
    player.Body().SetGrounded(true);
    player.Body().SetLateralVelocity(NS::Vector3{4.0f, 0.0f, 0.0f});
    player.UpdateAnimation();
    ASSERT_EQ(animation->CurrentClip(), 1u);
    animation->OnUpdate();
    const float first = animation->Time();
    EXPECT_NEAR(first, NS::OS::FrameTimer::FixedDelta() * 0.5f, 0.00001f);
    player.UpdateAnimation();
    animation->OnUpdate();
    EXPECT_NEAR(animation->Time(), first * 2.0f, 0.00001f);
    player.Body().SetLateralVelocity(NS::Vector3{2.0f, 0.0f, 0.0f});
    player.UpdateAnimation();
    EXPECT_EQ(animation->CurrentClip(), 2u);
    player.Body().SetLateralVelocity(NS::Vector3{});
    player.UpdateAnimation();
    EXPECT_EQ(animation->CurrentClip(), 0u);
}

TEST(PlayerAnimation, PlayerParamsKeepAllAnimatorNamesAndDefaults)
{
    Player player;
    player.EnsureInit();
    const NS::Game::Player::PlayerParams* params =
        NS::Obj::Cast<NS::Game::Player::PlayerParams>(player.FindSubObj("Params"));
    ASSERT_NE(params, nullptr);
    const nlohmann::json fields = NS::Obj::SerializeSubObjectFields(*params);
    const nlohmann::json expected = {{"立ちのクリップ", "idle"},
                                     {"歩きのクリップ", "walk"},
                                     {"走りのクリップ", "run"},
                                     {"跳ぶクリップ", ""},
                                     {"落ちるクリップ", ""},
                                     {"ぶら下がりのクリップ", ""},
                                     {"走りへ移る速さの比", 0.4f},
                                     {"再生速度の下限", 0.5f}};
    for (nlohmann::json::const_iterator it = expected.begin(); it != expected.end(); ++it)
    {
        ASSERT_TRUE(fields.contains(it.key())) << it.key();
        EXPECT_EQ(fields[it.key()], it.value()) << it.key();
    }
    EXPECT_EQ(player.FindSubObj("PlayerAnimator"), nullptr);
}

TEST(PlayerAnimation, AirborneClipsAndHangingStateUseLiveParams)
{
    const std::vector<NS::Gfx::AnimationClip> clips = {{.name = "idle", .duration = 10.0f},
                                                       {.name = "jump", .duration = 10.0f},
                                                       {.name = "fall", .duration = 10.0f},
                                                       {.name = "hang", .duration = 10.0f}};
    Player player;
    player.EnsureInit();
    NS::Obj::Animation* animation = NS::Obj::Cast<NS::Obj::Animation>(player.CreateSubObj("Animation"));
    animation->AddClips(clips);
    NS::Game::Player::PlayerParams* params =
        NS::Obj::Cast<NS::Game::Player::PlayerParams>(player.FindSubObj("Params"));
    ASSERT_NE(params, nullptr);
    ASSERT_EQ(NS::Obj::ApplyJsonFields(
                  *params, {{"跳ぶクリップ", "jump"}, {"落ちるクリップ", "fall"}, {"ぶら下がりのクリップ", "hang"}}),
              0u);
    player.Body().SetGrounded(false);
    player.Body().SetVerticalVelocity(2.0f);
    player.UpdateAnimation();
    EXPECT_EQ(animation->CurrentClip(), 1u);
    player.Body().SetVerticalVelocity(-2.0f);
    player.UpdateAnimation();
    EXPECT_EQ(animation->CurrentClip(), 2u);
    ASSERT_TRUE(player.States().Change<NS::Game::Player::LedgeHangingPlayerState>());
    player.UpdateAnimation();
    EXPECT_EQ(animation->CurrentClip(), 3u);
}

TEST(PlayerAnimation, MissingClipFallsBackToIdleAndSpeedFloorIsLive)
{
    const std::vector<NS::Gfx::AnimationClip> clips = {{.name = "idle", .duration = 10.0f},
                                                       {.name = "walk", .duration = 10.0f}};
    Player player;
    player.EnsureInit();
    NS::Obj::Animation* animation = NS::Obj::Cast<NS::Obj::Animation>(player.CreateSubObj("Animation"));
    animation->AddClips(clips);
    NS::Game::Player::PlayerParams* params =
        NS::Obj::Cast<NS::Game::Player::PlayerParams>(player.FindSubObj("Params"));
    ASSERT_NE(params, nullptr);
    player.Body().SetGrounded(true);
    player.Body().SetLateralVelocity(NS::Vector3{4.0f, 0.0f, 0.0f});
    player.UpdateAnimation();
    EXPECT_EQ(animation->CurrentClip(), 0u);
    player.Body().SetLateralVelocity(NS::Vector3{1.0f, 0.0f, 0.0f});
    ASSERT_EQ(NS::Obj::ApplyJsonFields(*params, {{"再生速度の下限", 0.75f}}), 0u);
    player.UpdateAnimation();
    ASSERT_EQ(animation->CurrentClip(), 1u);
    animation->OnUpdate();
    EXPECT_NEAR(animation->Time(), NS::OS::FrameTimer::FixedDelta() * 0.75f, 0.00001f);
}
