#include "Game/Player.h"
#include "NSlib/Object/IUse/IUseSceneObj.h"
#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Scene/HitScreenDirector.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/SubObjects/HitReaction.h"
#include "NSlib/Object/SubObjects/TransformSubObject.h"

#include <gtest/gtest.h>

#include <array>

// 白と震えの線は、シーンに 1 つの HitScreenDirector が進めて描く。部品は頼むだけ

namespace
{
    // 置物は、自機でない頼み手として使う
    Player* PlaceTwo(NS::Obj::Scene& scene)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json entry = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(entry, "Player");
        NS::Obj::SetObjectJsonId(entry, 1);
        NS::Obj::SetObjectPosition(entry, NS::Vector3{0.0f, 1.0f, 0.0f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(entry));
        nlohmann::json rock = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(rock, "MapObj");
        NS::Obj::SetObjectJsonId(rock, 2);
        NS::Obj::SetObjectPosition(rock, NS::Vector3{0.0f, 0.5f, 5.0f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
        scene.LoadJson(doc);
        return NS::Obj::Cast<Player>(scene.Objects().FindByObjectId(1));
    }
} // namespace

// 頼んだフレームは始めの濃さのまま。次の更新から、残りのフレーム数に比例して薄れる
TEST(HitScreenDirector, FlashStartsAtFullAlphaAndFadesFromTheNextTick)
{
    NS::Obj::Scene scene;
    Player* player = PlaceTwo(scene);
    ASSERT_NE(player, nullptr);
    NS::Obj::HitScreenDirector* screen = NS::Obj::FindSceneObj<NS::Obj::HitScreenDirector>(*player);
    ASSERT_NE(screen, nullptr);

    screen->StartFlash(*player, 3, 0.9f);
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

// 震えの線は始めたフレームの姿のまま出し、次の更新から数えて決めたフレーム数で消える
TEST(HitScreenDirector, ShakeLinesRunForTheirFrames)
{
    NS::Obj::Scene scene;
    Player* player = PlaceTwo(scene);
    ASSERT_NE(player, nullptr);
    NS::Obj::HitScreenDirector* screen = NS::Obj::FindSceneObj<NS::Obj::HitScreenDirector>(*player);
    ASSERT_NE(screen, nullptr);
    screen->StartShakeLines(*player, NS::Obj::HitShakeLinesDesc{.radius = 0.5f, .frames = 3});
    EXPECT_EQ(screen->ShakeLinesFramesRemaining(), 3);
    screen->OnTick();
    EXPECT_EQ(screen->ShakeLinesFramesRemaining(), 3);
    screen->OnTick();
    EXPECT_EQ(screen->ShakeLinesFramesRemaining(), 2);
    EXPECT_FLOAT_EQ(screen->ShakeLines().radius, 0.5f);
    // 半径が 0 以下・フレーム数 0 以下は出さない
    screen->StartShakeLines(*player, NS::Obj::HitShakeLinesDesc{.radius = 0.0f, .frames = 3});
    EXPECT_EQ(screen->ShakeLinesFramesRemaining(), 0);
}

// 止めると、止めた物が頼んだ白と線だけが消える。ほかの物が頼んだ物は残る
TEST(HitScreenDirector, StopClearsOnlyWhatTheRequesterStarted)
{
    NS::Obj::Scene scene;
    Player* player = PlaceTwo(scene);
    ASSERT_NE(player, nullptr);
    NS::Obj::Actor* rock = scene.Objects().FindByObjectId(2);
    ASSERT_NE(rock, nullptr);
    NS::Obj::HitScreenDirector* screen = NS::Obj::FindSceneObj<NS::Obj::HitScreenDirector>(*player);
    ASSERT_NE(screen, nullptr);

    screen->StartFlash(*rock, 3, 1.0f);
    screen->StartShakeLines(*player, NS::Obj::HitShakeLinesDesc{.radius = 0.5f, .frames = 3});
    screen->Stop(*player);
    EXPECT_EQ(screen->FlashFramesRemaining(), 3);
    EXPECT_EQ(screen->ShakeLinesFramesRemaining(), 0);
    screen->Stop(*rock);
    EXPECT_EQ(screen->FlashFramesRemaining(), 0);
}

// 当たりの演出の部品は HitScreenDirector へ頼む。部品を止めると白と線が消える
TEST(HitScreenDirector, HitReactionRequestsAndStopsThroughTheDirector)
{
    NS::Obj::Scene scene;
    Player* player = PlaceTwo(scene);
    ASSERT_NE(player, nullptr);
    NS::Obj::HitReaction* reaction = player->HitReactionSubObj();
    ASSERT_NE(reaction, nullptr);
    NS::Obj::HitScreenDirector* screen = NS::Obj::FindSceneObj<NS::Obj::HitScreenDirector>(*player);
    ASSERT_NE(screen, nullptr);
    reaction->StartFlash(4, 1.0f);
    reaction->StartShakeLines(NS::Obj::HitShakeLinesDesc{.radius = 0.5f, .frames = 4});
    EXPECT_EQ(screen->FlashFramesRemaining(), 4);
    EXPECT_EQ(screen->ShakeLinesFramesRemaining(), 4);
    reaction->Stop();
    EXPECT_EQ(screen->FlashFramesRemaining(), 0);
    EXPECT_EQ(screen->ShakeLinesFramesRemaining(), 0);
}

// 描く支度の段で進むので、自機以外を止めている間も薄れる
TEST(HitScreenDirector, FlashKeepsFadingWhileOthersAreHeld)
{
    NS::Obj::Scene scene;
    Player* player = PlaceTwo(scene);
    ASSERT_NE(player, nullptr);
    NS::Obj::HitScreenDirector* screen = NS::Obj::FindSceneObj<NS::Obj::HitScreenDirector>(*player);
    ASSERT_NE(screen, nullptr);
    scene.HoldOthers(10);
    screen->StartFlash(*player, 3, 1.0f);
    scene.OnUpdate();
    EXPECT_EQ(screen->FlashFramesRemaining(), 3);
    scene.OnUpdate();
    EXPECT_EQ(screen->FlashFramesRemaining(), 2);
}

// 震えの線は挟む物の輪郭の外の左右に 3 本ずつ。入れ替えのフレーム数ごとに、外と内へ交互にずれる
TEST(HitScreenDirector, ShakeLinesSitOutsideTheOutlineAndJitterByTheFlipFrames)
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
