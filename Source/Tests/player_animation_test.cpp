#include "Editor/EditorActors.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/States/LedgeHangingPlayerState.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/Animation/AnimClip.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/CoreObject/Action/ActorAction.h"
#include "NSlib/CoreObject/ClassObjs/Animation.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "TestAnimatedPlayerActor.h"

#include <gtest/gtest.h>

#include <string>
#include <variant>

// 自機は場面ごとに .actor の動作を名前で始め、その動作のアニメーションの項目のクリップを流す。
// 場面と流すクリップの組は、動作へ移す前の自機 (クリップの欄から選んでいた時) と同じ

namespace
{
    // 今のクリップの名前。クリップが無ければ空
    std::string CurrentClipName(const NS::Animation* animation)
    {
        NS_ASSERT(Game, animation != nullptr, "CurrentClipName: 渡したアクターか部品が無い");
        if (animation->CurrentClip() == nullptr)
        {
            return {};
        }
        return animation->CurrentClip()->name;
    }

    // 走っている動作の名前
    std::string CurrentActionName(const PlayerActor& player)
    {
        const NS::ActorAction* actions = player.ActorActionClassObj();
        NS_ASSERT(Game, actions != nullptr, "CurrentActionName: 自機が動作の部品を持たない");
        return std::string(actions->CurrentName());
    }

    PlayerActor& SpawnAnimatedPlayer(NS::Scene& scene)
    {
        PlayerActor* spawned = scene.SpawnActor<TestAnimatedPlayerActor>("TestAnimatedPlayerActor");
        NS_ASSERT(Game, spawned != nullptr, "SpawnAnimatedPlayer: 自機を置けなかった");
        return *spawned;
    }
} // namespace

TEST(PlayerAnimation, PlayerSelectsMovementActionWithoutRestartingItsTime)
{
    const ScopedPlayerActionClips clips({{"Wait", "Assets/Models/Xbot.glb#idle"},
                                         {"Walk", "Assets/Models/Xbot.glb#walk"},
                                         {"Run", "Assets/Models/Xbot.glb#run"}});
    NS::Scene scene;
    PlayerActor& player = SpawnAnimatedPlayer(scene);
    NS::Animation* animation = player.AnimationClassObj();
    player.Body().SetGrounded(true);
    player.Body().SetLateralVelocity(NS::Vector3{4.0f, 0.0f, 0.0f});
    player.UpdateAnimation();
    ASSERT_EQ(CurrentActionName(player), "Run");
    ASSERT_EQ(CurrentClipName(animation), "run");
    player.PrepareRender();
    const float first = animation->Time();
    EXPECT_NEAR(first, NS::OS::FrameTimer::FixedDelta() * 0.5f, 0.00001f);
    player.UpdateAnimation();
    player.PrepareRender();
    EXPECT_NEAR(animation->Time(), first * 2.0f, 0.00001f);
    player.Body().SetLateralVelocity(NS::Vector3{2.0f, 0.0f, 0.0f});
    player.UpdateAnimation();
    EXPECT_EQ(CurrentActionName(player), "Walk");
    EXPECT_EQ(CurrentClipName(animation), "walk");
    player.Body().SetLateralVelocity(NS::Vector3{});
    player.UpdateAnimation();
    EXPECT_EQ(CurrentActionName(player), "Wait");
    EXPECT_EQ(CurrentClipName(animation), "idle");
}

// クリップは .actor の動作 6 つのアニメーションの項目が持つ。歩きと走りの出し分けの値は見た目の部品 Appearance が持ち、
// 遊びの欄 Params と並ばない
TEST(PlayerAnimation, ClipsLiveInTheActionsAndGaitValuesInTheAppearance)
{
    NS::Scene scene;
    PlayerActor& player = SpawnAnimatedPlayer(scene);
    const nlohmann::json appearance = NS::SerializeClassObjFields(&player.Appearance());
    const nlohmann::json params = NS::SerializeClassObjFields(&player.Params());
    const nlohmann::json expected = {{"走りへ移る速さの比", 0.4f}, {"再生速度の下限", 0.5f}};
    for (nlohmann::json::const_iterator it = expected.begin(); it != expected.end(); ++it)
    {
        ASSERT_TRUE(appearance.contains(it.key())) << it.key();
        EXPECT_EQ(appearance[it.key()], it.value()) << it.key();
        EXPECT_FALSE(params.contains(it.key())) << it.key();
    }
    EXPECT_EQ(NS::ClassObjFields(NS::Editor::MakePrototypeJson(&player), "Clips"), nullptr);

    const NS::ActorAction& actions = *player.ActorActionClassObj();
    for (const char* name : {"Wait", "Walk", "Run", "Jump", "Fall", "LedgeHang"})
    {
        const NS::ActorActionInfo* action = actions.FindAction(name);
        ASSERT_NE(action, nullptr) << name;
        ASSERT_EQ(action->events.size(), 1u) << name;
        EXPECT_EQ(action->events.front().start, 0) << name;
        const NS::ActionAnimItem* anim = std::get_if<NS::ActionAnimItem>(&action->events.front().item);
        ASSERT_NE(anim, nullptr) << name;
        EXPECT_FLOAT_EQ(anim->blendSeconds, 0.12f) << name;
    }
}

TEST(PlayerAnimation, AirborneAndHangingStartTheirActions)
{
    const ScopedPlayerActionClips clips({{"Wait", "Assets/Models/Xbot.glb#idle"},
                                         {"Jump", "Assets/Models/Xbot.glb#agree"},
                                         {"Fall", "Assets/Models/Xbot.glb#headShake"},
                                         {"LedgeHang", "Assets/Models/Xbot.glb#sad_pose"}});
    NS::Scene scene;
    PlayerActor& player = SpawnAnimatedPlayer(scene);
    NS::Animation* animation = player.AnimationClassObj();
    player.Body().SetGrounded(false);
    player.Body().SetVerticalVelocity(2.0f);
    player.UpdateAnimation();
    EXPECT_EQ(CurrentActionName(player), "Jump");
    EXPECT_EQ(CurrentClipName(animation), "agree");
    player.Body().SetVerticalVelocity(-2.0f);
    player.UpdateAnimation();
    EXPECT_EQ(CurrentActionName(player), "Fall");
    EXPECT_EQ(CurrentClipName(animation), "headShake");
    ASSERT_TRUE(player.States().Change<GL::Player::LedgeHangingPlayerState>());
    player.UpdateAnimation();
    EXPECT_EQ(CurrentActionName(player), "LedgeHang");
    EXPECT_EQ(CurrentClipName(animation), "sad_pose");
}

// 名前はあるが読めないクリップの動作は始めず、立ちの動作 Wait を始める。再生速度の下限は動かしたまま効く
TEST(PlayerAnimation, MissingClipFallsBackToWaitAndSpeedFloorIsLive)
{
    const ScopedPlayerActionClips clips({{"Wait", "Assets/Models/Xbot.glb#idle"},
                                         {"Walk", "Assets/Models/Xbot.glb#walk"},
                                         {"Run", "Assets/Models/Xbot.glb#__ns_missing__"}});
    NS::Scene scene;
    PlayerActor& player = SpawnAnimatedPlayer(scene);
    NS::Animation* animation = player.AnimationClassObj();
    player.Body().SetGrounded(true);
    player.Body().SetLateralVelocity(NS::Vector3{4.0f, 0.0f, 0.0f});
    player.UpdateAnimation();
    EXPECT_EQ(CurrentActionName(player), "Wait");
    EXPECT_EQ(CurrentClipName(animation), "idle");
    player.Body().SetLateralVelocity(NS::Vector3{1.0f, 0.0f, 0.0f});
    ASSERT_EQ(NS::ApplyJsonFields(&player.Appearance(), {{"再生速度の下限", 0.75f}}), 0u);
    player.UpdateAnimation();
    ASSERT_EQ(CurrentClipName(animation), "walk");
    player.PrepareRender();
    EXPECT_NEAR(animation->Time(), NS::OS::FrameTimer::FixedDelta() * 0.75f, 0.00001f);
}

// 状態に結んだ動作 (エアダッシュ) が走っている間は、動きの動作で止めない。止めると残りの事象が起きなくなる
TEST(PlayerAnimation, MovementDoesNotReplaceAStateAction)
{
    const ScopedPlayerActionClips clips({{"Wait", "Assets/Models/Xbot.glb#idle"},
                                         {"Fall", "Assets/Models/Xbot.glb#headShake"}});
    NS::Scene scene;
    PlayerActor& player = SpawnAnimatedPlayer(scene);
    ASSERT_TRUE(player.StartStateAction("AirDash"));
    player.Body().SetGrounded(false);
    player.Body().SetVerticalVelocity(-2.0f);
    player.UpdateAnimation();
    EXPECT_EQ(CurrentActionName(player), "AirDash");
}
