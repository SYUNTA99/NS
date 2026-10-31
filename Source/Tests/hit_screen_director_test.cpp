#include "Editor/EditorActors.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IGet/IGetSceneSystem.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/System/HitScreenDirector.h"

#include <gtest/gtest.h>


namespace
{
    // 置物は、自機でない頼み手として使う
    PlayerActor* PlaceTwo(NS::Scene& scene)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json entry = NS::MakeActorJson();
        NS::SetActorJsonFile(entry, "PlayerActor");
        NS::SetActorJsonId(entry, 1);
        NS::Editor::SetActorJsonPosition(entry, NS::Vector3{0.0f, 1.0f, 0.0f});
        NS::SceneJsonActors(doc).push_back(std::move(entry));
        nlohmann::json rock = NS::MakeActorJson();
        NS::SetActorJsonFile(rock, "PropActor");
        NS::SetActorJsonId(rock, 2);
        NS::Editor::SetActorJsonPosition(rock, NS::Vector3{0.0f, 0.5f, 5.0f});
        NS::SceneJsonActors(doc).push_back(std::move(rock));
        scene.ResetFromJson(doc);
        return NS::Cast<PlayerActor>(scene.Actors().FindByActorId(1));
    }
} // namespace

// 頼んだフレームは始めの濃さのまま。次の更新から、残りのフレーム数に比例して薄れる
TEST(HitScreenDirector, FlashStartsAtFullAlphaAndFadesFromTheNextTick)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTwo(scene);
    ASSERT_NE(player, nullptr);
    NS::HitScreenDirector* screen = NS::FindSceneSystem<NS::HitScreenDirector>(player);
    ASSERT_NE(screen, nullptr);

    screen->StartFlash(player, 3, 0.9f);
    EXPECT_EQ(screen->FlashFramesRemaining(), 3);
    EXPECT_FLOAT_EQ(screen->FlashAlpha(), 0.9f);
    screen->OnTick();
    EXPECT_EQ(screen->FlashFramesRemaining(), 3);
    EXPECT_FLOAT_EQ(screen->FlashAlpha(), 0.9f);
    screen->OnTick();
    EXPECT_EQ(screen->FlashFramesRemaining(), 2);
    EXPECT_NEAR(screen->FlashAlpha(), 0.6f, 0.0001f);
    screen->OnTick();
    screen->OnTick();
    EXPECT_EQ(screen->FlashFramesRemaining(), 0);
    EXPECT_FLOAT_EQ(screen->FlashAlpha(), 0.0f);
}

TEST(HitScreenDirector, StopClearsOnlyWhatTheRequesterStarted)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTwo(scene);
    ASSERT_NE(player, nullptr);
    NS::SceneActor* rock = scene.Actors().FindByActorId(2);
    ASSERT_NE(rock, nullptr);
    NS::HitScreenDirector* screen = NS::FindSceneSystem<NS::HitScreenDirector>(player);
    ASSERT_NE(screen, nullptr);

    screen->StartFlash(rock, 3, 1.0f);
    screen->Stop(player);
    EXPECT_EQ(screen->FlashFramesRemaining(), 3);
    screen->Stop(rock);
    EXPECT_EQ(screen->FlashFramesRemaining(), 0);
}

TEST(HitScreenDirector, HitReactionRequestsAndStopsThroughTheDirector)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTwo(scene);
    ASSERT_NE(player, nullptr);
    NS::HitReaction* reaction = player->HitReactionClassObj();
    ASSERT_NE(reaction, nullptr);
    NS::HitScreenDirector* screen = NS::FindSceneSystem<NS::HitScreenDirector>(player);
    ASSERT_NE(screen, nullptr);
    reaction->StartFlash(player, 4, 1.0f);
    EXPECT_EQ(screen->FlashFramesRemaining(), 4);
    reaction->Stop(player);
    EXPECT_EQ(screen->FlashFramesRemaining(), 0);
}

// 描く支度の段で進むので、ほかを止めている間も薄れる
TEST(HitScreenDirector, FlashKeepsFadingWhileOthersAreHeld)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTwo(scene);
    ASSERT_NE(player, nullptr);
    NS::HitScreenDirector* screen = NS::FindSceneSystem<NS::HitScreenDirector>(player);
    ASSERT_NE(screen, nullptr);
    scene.HoldOthers(player, 10);
    screen->StartFlash(player, 3, 1.0f);
    scene.OnUpdate();
    EXPECT_EQ(screen->FlashFramesRemaining(), 3);
    scene.OnUpdate();
    EXPECT_EQ(screen->FlashFramesRemaining(), 2);
}

TEST(HitScreenDirector, DistortionRingWritesTheSceneForItsFrames)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTwo(scene);
    ASSERT_NE(player, nullptr);
    NS::HitScreenDirector* screen = NS::FindSceneSystem<NS::HitScreenDirector>(player);
    ASSERT_NE(screen, nullptr);
    NS::HitDistortionRingDesc desc;
    desc.center = NS::Vector3{1.0f, 2.0f, 3.0f};
    desc.radius.count = 2;
    desc.radius.keys[0] = NS::Curve::Key{0.0f, 0.0f};
    desc.radius.keys[1] = NS::Curve::Key{4.0f, 0.4f};
    desc.push.count = 1;
    desc.push.keys[0] = NS::Curve::Key{0.0f, 0.03f};
    desc.halfWidth = 0.05f;
    desc.frames = 3;

    screen->StartDistortionRing(player, desc);
    for (int frame = 0; frame < 3; ++frame)
    {
        SCOPED_TRACE(frame);
        const NS::Gfx::DistortionRing& now = scene.DistortionRingShown();
        EXPECT_TRUE(now.center == desc.center);
        EXPECT_NEAR(now.radius, 0.1f * static_cast<float>(frame), 1.0e-6f);
        EXPECT_FLOAT_EQ(now.push, 0.03f);
        EXPECT_FLOAT_EQ(now.halfWidth, 0.05f);
        screen->OnTick();
        if (frame == 0)
        {
            // 頼んだフレームの更新では進めない
            EXPECT_FLOAT_EQ(scene.DistortionRingShown().radius, 0.0f);
            screen->OnTick();
        }
    }
    EXPECT_FLOAT_EQ(scene.DistortionRingShown().push, 0.0f);
    // フレーム数 0 以下は出さない
    desc.frames = 0;
    screen->StartDistortionRing(player, desc);
    EXPECT_FLOAT_EQ(scene.DistortionRingShown().push, 0.0f);
}

// 歪みの輪は、頼んだ物が止めた時だけ消える
TEST(HitScreenDirector, DistortionRingStopsOnlyForItsRequester)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTwo(scene);
    ASSERT_NE(player, nullptr);
    NS::SceneActor* rock = scene.Actors().FindByActorId(2);
    ASSERT_NE(rock, nullptr);
    NS::HitReaction* reaction = player->HitReactionClassObj();
    ASSERT_NE(reaction, nullptr);
    NS::HitScreenDirector* screen = NS::FindSceneSystem<NS::HitScreenDirector>(player);
    ASSERT_NE(screen, nullptr);
    NS::HitDistortionRingDesc desc;
    desc.push.count = 1;
    desc.push.keys[0] = NS::Curve::Key{0.0f, 0.03f};
    desc.frames = 10;

    reaction->StartDistortionRing(player, desc);
    EXPECT_FLOAT_EQ(scene.DistortionRingShown().push, 0.03f);
    screen->StopDistortionRing(rock);
    EXPECT_FLOAT_EQ(scene.DistortionRingShown().push, 0.03f);
    reaction->Stop(player);
    EXPECT_FLOAT_EQ(scene.DistortionRingShown().push, 0.03f);
    reaction->StopDistortionRing(player);
    EXPECT_FLOAT_EQ(scene.DistortionRingShown().push, 0.0f);
}
