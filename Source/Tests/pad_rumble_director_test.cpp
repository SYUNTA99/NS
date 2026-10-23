#include "Editor/EditorActors.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IUse/IUseSceneSystem.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/System/PadRumbleDirector.h"
#include "NSlib/Windows/Input.h"

#include <gtest/gtest.h>

// パッドの振動は、シーンに 1 つの PadRumbleDirector が進めて書く。部品は頼むだけ

namespace
{
    PlayerActor* PlacePlayer(NS::Scene& scene)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json entry = NS::MakeActorJson();
        NS::SetActorJsonFile(entry, "PlayerActor");
        NS::SetActorJsonId(entry, 1);
        NS::Editor::SetActorJsonPosition(entry, NS::Vector3{0.0f, 1.0f, 0.0f});
        NS::SceneJsonActors(doc).push_back(std::move(entry));
        scene.ResetFromJson(doc);
        return NS::Cast<PlayerActor>(scene.Actors().FindByActorId(1));
    }

    float PadLeft()
    {
        return NS::OS::Input::Get().Gamepad().Vibration().left;
    }

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

// 頼んだフレームに曲線の始めを書き、次の更新から曲線どおりに進む。重ねた振動は足し、それぞれの長さで終わる
TEST(PadRumbleDirector, BlendedVibrationsAddAndEndOnTheirOwnLength)
{
    NS::Scene scene;
    PlayerActor* player = PlacePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::PadRumbleDirector* pad = NS::FindSceneSystem<NS::PadRumbleDirector>(player);
    ASSERT_NE(pad, nullptr);

    const NS::HitPadVibration first = FadingLeft(0.5f, 4);
    const NS::HitPadVibration second = FadingLeft(0.25f, 2);
    pad->Start(*player, first);
    pad->Blend(*player, second);
    EXPECT_FLOAT_EQ(PadLeft(), 0.75f);
    // 頼んだフレームの更新は最初の姿のまま
    pad->OnTick();
    EXPECT_FLOAT_EQ(PadLeft(), 0.75f);
    pad->OnTick();
    EXPECT_FLOAT_EQ(PadLeft(), 0.5f);
    pad->OnTick();
    // 後の振動は 2 フレームで終わり、前の振動だけが残る
    EXPECT_FLOAT_EQ(PadLeft(), 0.25f);
    pad->OnTick();
    EXPECT_FLOAT_EQ(PadLeft(), 0.125f);
    pad->OnTick();
    EXPECT_FLOAT_EQ(PadLeft(), 0.0f);

    pad->Start(*player, first);
    pad->Blend(*player, second);
    pad->Start(*player, FadingLeft(0.2f, 4));
    EXPECT_FLOAT_EQ(PadLeft(), 0.2f);
}

TEST(PadRumbleDirector, HitReactionStopWritesZero)
{
    NS::Scene scene;
    PlayerActor* player = PlacePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::HitReaction* reaction = player->HitReactionClassObj();
    ASSERT_NE(reaction, nullptr);
    reaction->StartPadVibration(FadingLeft(1.0f, 4));
    EXPECT_FLOAT_EQ(PadLeft(), 1.0f);
    reaction->Stop();
    EXPECT_FLOAT_EQ(PadLeft(), 0.0f);
}

// PadRumbleDirector を捨てる時に 0 を書く。振動の途中で場面を捨てても、パッドが震え続けない
TEST(PadRumbleDirector, DiscardingTheDirectorWritesZero)
{
    NS::Scene scene;
    PlayerActor* player = PlacePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::PadRumbleDirector* pad = NS::FindSceneSystem<NS::PadRumbleDirector>(player);
    ASSERT_NE(pad, nullptr);
    pad->Start(*player, FadingLeft(1.0f, 30));
    EXPECT_FLOAT_EQ(PadLeft(), 1.0f);
    scene.GetSceneSystemHolder()->Clear();
    EXPECT_FLOAT_EQ(PadLeft(), 0.0f);
}

// 重ねた振動の和が 1 を超えても 1 で書き、振動を止めない
TEST(PadRumbleDirector, BlendedSumAboveOneWritesOneAndKeepsRunning)
{
    NS::Scene scene;
    PlayerActor* player = PlacePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::PadRumbleDirector* pad = NS::FindSceneSystem<NS::PadRumbleDirector>(player);
    ASSERT_NE(pad, nullptr);

    pad->Start(*player, FadingLeft(0.75f, 4));
    pad->Blend(*player, FadingLeft(0.5f, 4));
    EXPECT_FLOAT_EQ(PadLeft(), 1.0f);
    pad->OnTick();
    pad->OnTick();
    // 和が 0.9375 へ下がったフレームも書く
    EXPECT_FLOAT_EQ(PadLeft(), 0.9375f);
}
