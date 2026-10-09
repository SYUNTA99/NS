#include "Game/Player.h"
#include "NSlib/Object/IUse/IUseSceneObj.h"
#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Scene/HitScreenDirector.h"
#include "NSlib/Object/Scene/PadRumbleDirector.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/SubObjects/HitReaction.h"
#include "NSlib/Object/SubObjects/TransformSubObject.h"
#include "NSlib/Windows/Input.h"

#include <gtest/gtest.h>

// 当たりの演出の白・揺れ・寄り・振動は、別々に頼め、頼んだ物だけが変わる

namespace
{
    Player* PlacePlayer(NS::Obj::Scene& scene)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json entry = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(entry, "Player");
        NS::Obj::SetObjectJsonId(entry, 1);
        NS::Obj::SetObjectPosition(entry, NS::Vector3{0.0f, 1.0f, 0.0f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(entry));
        scene.LoadJson(doc);
        return NS::Obj::Cast<Player>(scene.Objects().FindByObjectId(1));
    }

    float PadLeft()
    {
        return NS::OS::Input::Get().Gamepad().Vibration().left;
    }

    // 左のモーターを start から frames フレームで 0 へ下げる振動
    NS::Obj::HitPadVibration FadingLeft(float start, int frames)
    {
        NS::Obj::HitPadVibration pad;
        pad.left.count = 2;
        pad.left.keys[0] = NS::Obj::Curve::Key{0.0f, start};
        pad.left.keys[1] = NS::Obj::Curve::Key{static_cast<float>(frames), 0.0f};
        pad.frames = frames;
        return pad;
    }
} // namespace

// 白を頼んでも書いている振動は続き、振動を頼んでも薄れている白は続く
TEST(HitReactionChannels, StartingOneChannelLeavesTheOthersRunning)
{
    NS::Obj::Scene scene;
    Player* player = PlacePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::Obj::HitReaction* reaction = player->HitReactionSubObj();
    ASSERT_NE(reaction, nullptr);
    NS::Obj::HitScreenDirector* screen = NS::Obj::FindSceneObj<NS::Obj::HitScreenDirector>(*player);
    NS::Obj::PadRumbleDirector* pad = NS::Obj::FindSceneObj<NS::Obj::PadRumbleDirector>(*player);
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
    NS::Obj::Scene scene;
    Player* player = PlacePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::Obj::HitReaction* reaction = player->HitReactionSubObj();
    ASSERT_NE(reaction, nullptr);
    NS::Obj::HitScreenDirector* screen = NS::Obj::FindSceneObj<NS::Obj::HitScreenDirector>(*player);
    ASSERT_NE(screen, nullptr);
    EXPECT_TRUE(reaction->StartShake(NS::Obj::CameraShakeDesc{.upAmplitude = 0.1f, .frames = 4}));
    EXPECT_EQ(screen->FlashFramesRemaining(), 0);
    EXPECT_TRUE(reaction->StartZoomRoll(NS::Obj::CameraZoomRollDesc{.zoom = 1.2f, .holdFrames = 2, .returnFrames = 2}));
    EXPECT_FALSE(reaction->StartShake(NS::Obj::CameraShakeDesc{.upAmplitude = 0.1f, .frames = 61}));
}
