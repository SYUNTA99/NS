#include "Game/Player.h"
#include "Runtime/Object/Components/HitReaction.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/ObjectJson.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Platform/Input.h"

#include <gtest/gtest.h>

// 当たりの演出の白・揺れ・寄り・振動は、別々に始められ、始めた物だけが変わる

namespace
{
    NS::Obj::HitReaction* PlaceReaction(NS::Obj::Scene& scene)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json entry = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(entry, "Player");
        NS::Obj::SetObjectJsonId(entry, 1);
        NS::Obj::SetObjectPosition(entry, NS::Core::Vector3{0.0f, 1.0f, 0.0f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(entry));
        scene.LoadJson(doc);
        Player* player = NS::Obj::Cast<Player>(scene.Objects().FindByObjectId(1));
        if (player == nullptr)
        {
            return nullptr;
        }
        return player->HitReactionPart();
    }

    float PadLeft()
    {
        return NS::Platform::Input::Get().Gamepad(0).Vibration().left;
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

// 白を始めても書いている振動は続き、振動を始めても薄れている白は続く
TEST(HitReactionChannels, StartingOneChannelLeavesTheOthersRunning)
{
    NS::Obj::Scene scene;
    NS::Obj::HitReaction* reaction = PlaceReaction(scene);
    ASSERT_NE(reaction, nullptr);

    reaction->StartPadVibration(FadingLeft(1.0f, 4));
    EXPECT_FLOAT_EQ(PadLeft(), 1.0f);
    reaction->OnUpdate();
    reaction->OnUpdate();
    EXPECT_FLOAT_EQ(PadLeft(), 0.75f);

    reaction->StartFlash(3, 1.0f);
    EXPECT_EQ(reaction->FlashFramesRemaining(), 3);
    reaction->OnUpdate();
    // 白は始めたフレームの姿のまま、振動は進む
    EXPECT_EQ(reaction->FlashFramesRemaining(), 3);
    EXPECT_FLOAT_EQ(PadLeft(), 0.5f);

    reaction->StartPadVibration(FadingLeft(0.5f, 2));
    reaction->OnUpdate();
    // 振動は始めたフレームの姿のまま、白は薄れる
    EXPECT_EQ(reaction->FlashFramesRemaining(), 2);
    EXPECT_FLOAT_EQ(PadLeft(), 0.5f);
    reaction->OnUpdate();
    EXPECT_FLOAT_EQ(PadLeft(), 0.25f);

    reaction->Stop();
    EXPECT_EQ(reaction->FlashFramesRemaining(), 0);
    EXPECT_FLOAT_EQ(PadLeft(), 0.0f);
}

// 揺れと寄りは、始めた物だけがカメラの効果に積まれる
TEST(HitReactionChannels, ShakeAndZoomStartSeparately)
{
    NS::Obj::Scene scene;
    NS::Obj::HitReaction* reaction = PlaceReaction(scene);
    ASSERT_NE(reaction, nullptr);
    EXPECT_TRUE(reaction->StartShake(NS::Obj::CameraShakeDesc{.upAmplitude = 0.1f, .frames = 4}));
    EXPECT_EQ(reaction->FlashFramesRemaining(), 0);
    EXPECT_TRUE(reaction->StartZoomRoll(NS::Obj::CameraZoomRollDesc{.zoom = 1.2f, .holdFrames = 2, .returnFrames = 2}));
    EXPECT_FALSE(reaction->StartShake(NS::Obj::CameraShakeDesc{.upAmplitude = 0.1f, .frames = 61}));
}

// 重ねた振動は足し、それぞれの長さで終わる。始め直すと重ねた分も消える
TEST(HitReactionChannels, BlendedPadVibrationsAddAndEndOnTheirOwnLength)
{
    NS::Obj::Scene scene;
    NS::Obj::HitReaction* reaction = PlaceReaction(scene);
    ASSERT_NE(reaction, nullptr);

    const NS::Obj::HitPadVibration first = FadingLeft(0.5f, 4);
    const NS::Obj::HitPadVibration second = FadingLeft(0.25f, 2);
    reaction->StartPadVibration(first);
    reaction->BlendPadVibration(second);
    EXPECT_FLOAT_EQ(PadLeft(), 0.75f);
    // 始めたフレームの更新は最初の姿のまま
    reaction->OnUpdate();
    EXPECT_FLOAT_EQ(PadLeft(), 0.75f);
    reaction->OnUpdate();
    EXPECT_FLOAT_EQ(PadLeft(), 0.5f);
    reaction->OnUpdate();
    // 後の振動は 2 フレームで終わり、前の振動だけが残る
    EXPECT_FLOAT_EQ(PadLeft(), 0.25f);
    reaction->OnUpdate();
    EXPECT_FLOAT_EQ(PadLeft(), 0.125f);
    reaction->OnUpdate();
    EXPECT_FLOAT_EQ(PadLeft(), 0.0f);

    reaction->StartPadVibration(first);
    reaction->BlendPadVibration(second);
    reaction->StartPadVibration(FadingLeft(0.2f, 4));
    EXPECT_FLOAT_EQ(PadLeft(), 0.2f);
}
