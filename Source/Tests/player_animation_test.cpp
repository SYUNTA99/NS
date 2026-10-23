#include "Editor/EditorActors.h"
#include "Game/Player/PlayerClips.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/States/LedgeHangingPlayerState.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/Animation/AnimClip.h"
#include "NSlib/CoreObject/ClassObjs/Animation.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/Windows/Clock.h"
#include "TestAnimatedPlayerActor.h"

#include <gtest/gtest.h>

#include <string>

namespace
{
    // 今のクリップの名前。クリップが無ければ空
    std::string CurrentClipName(const NS::Animation& animation)
    {
        if (animation.CurrentClip() == nullptr)
        {
            return {};
        }
        return animation.CurrentClip()->name;
    }
} // namespace

TEST(PlayerAnimation, PlayerSelectsMovementClipWithoutRestartingItsTime)
{
    const std::unique_ptr<PlayerActor> playerOwned = NS::CreateActor<TestAnimatedPlayerActor>();
    PlayerActor& player = *playerOwned;
    ASSERT_EQ(UseTestClips(player,
                           {{"立ちのクリップ", "Assets/Models/Xbot.glb#idle"},
                            {"歩きのクリップ", "Assets/Models/Xbot.glb#walk"},
                            {"走りのクリップ", "Assets/Models/Xbot.glb#run"}}),
              0u);
    NS::Animation* animation = player.AnimationClassObj();
    player.Body().SetGrounded(true);
    player.Body().SetLateralVelocity(NS::Vector3{4.0f, 0.0f, 0.0f});
    player.UpdateAnimation();
    ASSERT_EQ(CurrentClipName(*animation), "run");
    animation->Update();
    const float first = animation->Time();
    EXPECT_NEAR(first, NS::OS::FrameTimer::FixedDelta() * 0.5f, 0.00001f);
    player.UpdateAnimation();
    animation->Update();
    EXPECT_NEAR(animation->Time(), first * 2.0f, 0.00001f);
    player.Body().SetLateralVelocity(NS::Vector3{2.0f, 0.0f, 0.0f});
    player.UpdateAnimation();
    EXPECT_EQ(CurrentClipName(*animation), "walk");
    player.Body().SetLateralVelocity(NS::Vector3{});
    player.UpdateAnimation();
    EXPECT_EQ(CurrentClipName(*animation), "idle");
}

// アニメの欄はアニメの欄だけの部品 Clips が持ち、遊びの欄と並ばない。モデルを入れるまで字面は空
TEST(PlayerAnimation, ClipsKeepAllAnimatorNamesAndDefaults)
{
    const std::unique_ptr<PlayerActor> playerOwned = NS::CreateActor<TestAnimatedPlayerActor>();
    PlayerActor& player = *playerOwned;
    const nlohmann::json fields = NS::SerializeClassObjFields(player.Clips());
    const nlohmann::json paramFields = NS::SerializeClassObjFields(player.Params());
    const nlohmann::json expected = {{"立ちのクリップ", ""},
                                     {"歩きのクリップ", ""},
                                     {"走りのクリップ", ""},
                                     {"跳ぶクリップ", ""},
                                     {"落ちるクリップ", ""},
                                     {"ぶら下がりのクリップ", ""},
                                     {"走りへ移る速さの比", 0.4f},
                                     {"再生速度の下限", 0.5f}};
    for (nlohmann::json::const_iterator it = expected.begin(); it != expected.end(); ++it)
    {
        ASSERT_TRUE(fields.contains(it.key())) << it.key();
        EXPECT_EQ(fields[it.key()], it.value()) << it.key();
        EXPECT_FALSE(paramFields.contains(it.key())) << it.key();
    }
    EXPECT_EQ(NS::ClassObjFields(NS::Editor::MakePrototypeJson(player), "PlayerAnimator"), nullptr);
}

TEST(PlayerAnimation, AirborneClipsAndHangingStateUseLiveParams)
{
    const std::unique_ptr<PlayerActor> playerOwned = NS::CreateActor<TestAnimatedPlayerActor>();
    PlayerActor& player = *playerOwned;
    NS::Animation* animation = player.AnimationClassObj();
    ASSERT_EQ(UseTestClips(player,
                           {{"立ちのクリップ", "Assets/Models/Xbot.glb#idle"},
                            {"跳ぶクリップ", "Assets/Models/Xbot.glb#agree"},
                            {"落ちるクリップ", "Assets/Models/Xbot.glb#headShake"},
                            {"ぶら下がりのクリップ", "Assets/Models/Xbot.glb#sad_pose"}}),
              0u);
    player.Body().SetGrounded(false);
    player.Body().SetVerticalVelocity(2.0f);
    player.UpdateAnimation();
    EXPECT_EQ(CurrentClipName(*animation), "agree");
    player.Body().SetVerticalVelocity(-2.0f);
    player.UpdateAnimation();
    EXPECT_EQ(CurrentClipName(*animation), "headShake");
    ASSERT_TRUE(player.States().Change<GL::Player::LedgeHangingPlayerState>());
    player.UpdateAnimation();
    EXPECT_EQ(CurrentClipName(*animation), "sad_pose");
}

// 字面はあるが読めないクリップは、立ちのクリップへ戻す
TEST(PlayerAnimation, MissingClipFallsBackToIdleAndSpeedFloorIsLive)
{
    const std::unique_ptr<PlayerActor> playerOwned = NS::CreateActor<TestAnimatedPlayerActor>();
    PlayerActor& player = *playerOwned;
    NS::Animation* animation = player.AnimationClassObj();
    ASSERT_EQ(UseTestClips(player,
                           {{"立ちのクリップ", "Assets/Models/Xbot.glb#idle"},
                            {"歩きのクリップ", "Assets/Models/Xbot.glb#walk"},
                            {"走りのクリップ", "Assets/Models/Xbot.glb#__ns_missing__"}}),
              0u);
    player.Body().SetGrounded(true);
    player.Body().SetLateralVelocity(NS::Vector3{4.0f, 0.0f, 0.0f});
    player.UpdateAnimation();
    EXPECT_EQ(CurrentClipName(*animation), "idle");
    player.Body().SetLateralVelocity(NS::Vector3{1.0f, 0.0f, 0.0f});
    ASSERT_EQ(UseTestClips(player, {{"再生速度の下限", 0.75f}}), 0u);
    player.UpdateAnimation();
    ASSERT_EQ(CurrentClipName(*animation), "walk");
    animation->Update();
    EXPECT_NEAR(animation->Time(), NS::OS::FrameTimer::FixedDelta() * 0.75f, 0.00001f);
}
