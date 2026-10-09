#include "Game/Player.h"
#include "NSlib/Object/IUse/IUseSceneObj.h"
#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Scene/PadRumbleDirector.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/SubObjects/HitReaction.h"
#include "NSlib/Object/SubObjects/TransformSubObject.h"
#include "NSlib/Windows/Input.h"

#include <gtest/gtest.h>

// パッドの振動は、シーンに 1 つの PadRumbleDirector が進めて書く。部品は頼むだけ

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

// 頼んだフレームに曲線の始めを書き、次の更新から曲線どおりに進む。重ねた振動は足し、それぞれの長さで終わる
TEST(PadRumbleDirector, BlendedVibrationsAddAndEndOnTheirOwnLength)
{
    NS::Obj::Scene scene;
    Player* player = PlacePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::Obj::PadRumbleDirector* pad = NS::Obj::FindSceneObj<NS::Obj::PadRumbleDirector>(*player);
    ASSERT_NE(pad, nullptr);

    const NS::Obj::HitPadVibration first = FadingLeft(0.5f, 4);
    const NS::Obj::HitPadVibration second = FadingLeft(0.25f, 2);
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
    NS::Obj::Scene scene;
    Player* player = PlacePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::Obj::HitReaction* reaction = player->HitReactionSubObj();
    ASSERT_NE(reaction, nullptr);
    reaction->StartPadVibration(FadingLeft(1.0f, 4));
    EXPECT_FLOAT_EQ(PadLeft(), 1.0f);
    reaction->Stop();
    EXPECT_FLOAT_EQ(PadLeft(), 0.0f);
}

// PadRumbleDirector を捨てる時に 0 を書く。振動の途中で場面を捨てても、パッドが震え続けない
TEST(PadRumbleDirector, DiscardingTheDirectorWritesZero)
{
    NS::Obj::Scene scene;
    Player* player = PlacePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::Obj::PadRumbleDirector* pad = NS::Obj::FindSceneObj<NS::Obj::PadRumbleDirector>(*player);
    ASSERT_NE(pad, nullptr);
    pad->Start(*player, FadingLeft(1.0f, 30));
    EXPECT_FLOAT_EQ(PadLeft(), 1.0f);
    scene.GetSceneObjHolder()->Clear();
    EXPECT_FLOAT_EQ(PadLeft(), 0.0f);
}
