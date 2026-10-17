#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IUse/IUseSceneSystem.h"
#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/System/HitScreenDirector.h"
#include "NSlib/CoreObject/System/PadRumbleDirector.h"
#include "NSlib/Windows/Input.h"

#include <gtest/gtest.h>

// 当たりの演出の白・揺れ・寄り・振動は、別々に頼め、頼んだ物だけが変わる

namespace
{
    PlayerActor* PlacePlayer(NS::Scene& scene)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json entry = NS::MakeObjectJson();
        NS::SetObjectJsonClass(entry, "PlayerActor");
        NS::SetObjectJsonId(entry, 1);
        NS::SetObjectPosition(entry, NS::Vector3{0.0f, 1.0f, 0.0f});
        NS::SceneJsonObjects(doc).push_back(std::move(entry));
        scene.ResetFromJson(doc);
        return NS::Cast<PlayerActor>(scene.Objects().FindByObjectId(1));
    }

    float PadLeft()
    {
        return NS::OS::Input::Get().Gamepad().Vibration().left;
    }

    // 左のモーターを start から frames フレームで 0 へ下げる振動
    NS::HitPadVibration FadingLeft(float start, int frames)
    {
        NS::HitPadVibration pad;
        pad.left.count = 2;
        pad.left.keys[0] = NS::Curve::Key{0.0f, start};
        pad.left.keys[1] = NS::Curve::Key{static_cast<float>(frames), 0.0f};
        pad.frames = frames;
        return pad;
    }
} // namespace

// 白を頼んでも書いている振動は続き、振動を頼んでも薄れている白は続く
TEST(HitReactionChannels, StartingOneChannelLeavesTheOthersRunning)
{
    NS::Scene scene;
    PlayerActor* player = PlacePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::HitReaction* reaction = player->HitReactionClassObj();
    ASSERT_NE(reaction, nullptr);
    NS::HitScreenDirector* screen = NS::FindSceneSystem<NS::HitScreenDirector>(player);
    NS::PadRumbleDirector* pad = NS::FindSceneSystem<NS::PadRumbleDirector>(player);
    ASSERT_NE(screen, nullptr);
    ASSERT_NE(pad, nullptr);
    const auto tick = [&]() {
        screen->OnTick();
        pad->OnTick();
    };

    reaction->StartPadVibration(FadingLeft(1.0f, 4));
    EXPECT_FLOAT_EQ(PadLeft(), 1.0f);
    tick();
    tick();
    EXPECT_FLOAT_EQ(PadLeft(), 0.75f);

    reaction->StartFlash(3, 1.0f);
    EXPECT_EQ(screen->FlashFramesRemaining(), 3);
    tick();
    EXPECT_EQ(screen->FlashFramesRemaining(), 3);
    EXPECT_FLOAT_EQ(PadLeft(), 0.5f);

    reaction->StartPadVibration(FadingLeft(0.5f, 2));
    tick();
    // 振動は頼んだフレームの姿のまま、白は薄れる
    EXPECT_EQ(screen->FlashFramesRemaining(), 2);
    EXPECT_FLOAT_EQ(PadLeft(), 0.5f);
    tick();
    EXPECT_FLOAT_EQ(PadLeft(), 0.25f);

    reaction->Stop();
    EXPECT_EQ(screen->FlashFramesRemaining(), 0);
    EXPECT_FLOAT_EQ(PadLeft(), 0.0f);
}

// 揺れと寄りは、頼んだ物だけがカメラの効果に積まれる
TEST(HitReactionChannels, ShakeAndZoomStartSeparately)
{
    NS::Scene scene;
    PlayerActor* player = PlacePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::HitReaction* reaction = player->HitReactionClassObj();
    ASSERT_NE(reaction, nullptr);
    NS::HitScreenDirector* screen = NS::FindSceneSystem<NS::HitScreenDirector>(player);
    ASSERT_NE(screen, nullptr);
    EXPECT_TRUE(reaction->StartShake(NS::CameraShakeDesc{.upAmplitude = 0.1f, .frames = 4}));
    EXPECT_EQ(screen->FlashFramesRemaining(), 0);
    EXPECT_TRUE(reaction->StartZoomRoll(NS::CameraZoomRollDesc{.zoom = 1.2f, .holdFrames = 2, .returnFrames = 2}));
    EXPECT_FALSE(reaction->StartShake(NS::CameraShakeDesc{.upAmplitude = 0.1f, .frames = 61}));
}
