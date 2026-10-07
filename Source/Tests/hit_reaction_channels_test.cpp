#include "Game/Player.h"
#include "NSlib/Object/Components/HitReaction.h"
#include "NSlib/Object/Components/TransformComponent.h"
#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Windows/Input.h"

#include <gtest/gtest.h>

#include <array>

// 当たりの演出の白・揺れ・寄り・振動は、別々に始められ、始めた物だけが変わる

namespace
{
    NS::Obj::HitReaction* PlaceReaction(NS::Obj::Scene& scene)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json entry = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(entry, "Player");
        NS::Obj::SetObjectJsonId(entry, 1);
        NS::Obj::SetObjectPosition(entry, NS::Vector3{0.0f, 1.0f, 0.0f});
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

// 震えの線は始めたフレームの姿のまま出し、次の更新から数えて決めたフレーム数で消える。止めると消える
TEST(HitReactionChannels, ShakeLinesRunForTheirFramesAndStop)
{
    NS::Obj::Scene scene;
    NS::Obj::HitReaction* reaction = PlaceReaction(scene);
    ASSERT_NE(reaction, nullptr);
    reaction->StartShakeLines(NS::Obj::HitShakeLinesDesc{.radius = 0.5f, .frames = 3});
    EXPECT_EQ(reaction->ShakeLinesFramesRemaining(), 3);
    reaction->OnUpdate();
    EXPECT_EQ(reaction->ShakeLinesFramesRemaining(), 3);
    reaction->OnUpdate();
    EXPECT_EQ(reaction->ShakeLinesFramesRemaining(), 2);
    reaction->Stop();
    EXPECT_EQ(reaction->ShakeLinesFramesRemaining(), 0);
    // 半径が 0 以下・フレーム数 0 以下は出さない
    reaction->StartShakeLines(NS::Obj::HitShakeLinesDesc{.radius = 0.0f, .frames = 3});
    EXPECT_EQ(reaction->ShakeLinesFramesRemaining(), 0);
}

// 震えの線は挟む物の輪郭の外の左右に 3 本ずつ。入れ替えのフレーム数ごとに、外と内へ交互にずれる
TEST(HitReactionChannels, ShakeLinesSitOutsideTheOutlineAndJitterByTheFlipFrames)
{
    const NS::Obj::HitShakeLinesDesc desc{.radius = 0.5f, .frames = 12, .flipFrames = 2};
    const NS::Obj::HitShakeLineSpan span{.left = 540.0f, .right = 760.0f, .centerY = 360.0f};
    const std::array<NS::Obj::HitShakeLineRect, 6> first = NS::Obj::ShakeLineRects(desc, 0, span, 1.0f);
    int left = 0;
    int right = 0;
    for (const NS::Obj::HitShakeLineRect& rect : first)
    {
        EXPECT_GT(rect.width, 0.0f);
        EXPECT_GT(rect.height, rect.width);
        EXPECT_FLOAT_EQ(rect.y + rect.height * 0.5f, span.centerY);
        if (rect.x + rect.width <= span.left)
        {
            ++left;
        }
        if (rect.x >= span.right)
        {
            ++right;
        }
    }
    EXPECT_EQ(left, 3);
    EXPECT_EQ(right, 3);
    const std::array<NS::Obj::HitShakeLineRect, 6> same = NS::Obj::ShakeLineRects(desc, 1, span, 1.0f);
    const std::array<NS::Obj::HitShakeLineRect, 6> flipped = NS::Obj::ShakeLineRects(desc, 2, span, 1.0f);
    for (std::size_t i = 0; i < first.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_FLOAT_EQ(same[i].x, first[i].x);
        EXPECT_NE(flipped[i].x, first[i].x);
    }
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
