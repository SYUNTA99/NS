#include "Game/Level/LaunchArc.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/MapObj.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Scene/SceneJson.h"

#include <gtest/gtest.h>

// 体当たりを受けた置物が、知らせだけで答え・食い込み・戻り・飛ぶことを縛る

namespace
{
    NS::Obj::Actor* PlaceRock(NS::Obj::Scene& scene, const NS::Core::Vector3& position)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json rock = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(rock, "MapObj");
        NS::Obj::SetObjectJsonId(rock, 1);
        NS::Obj::SetObjectPosition(rock, position);
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
        scene.LoadJson(doc);
        return scene.Objects().FindByObjectId(1);
    }
} // namespace

TEST(MapObjTackle, RockAnswersTheAsk)
{
    NS::Obj::Scene scene;
    NS::Obj::Actor* rock = PlaceRock(scene, NS::Core::Vector3{2.0f, 1.0f, 0.0f});
    ASSERT_NE(rock, nullptr);
    NS::Obj::HitSensor* body = rock->BodySensorPart();
    ASSERT_NE(body, nullptr);

    NS::Game::Level::TackleTargetAnswer answer{};
    ASSERT_TRUE(NS::Game::Level::SendMsgAskTackleTarget(*body, answer));
    EXPECT_TRUE(answer.placed);
    // 壊れる動きはまだ無いので、破壊を許しても自機を貫通させない
    EXPECT_FALSE(answer.breakable);
    EXPECT_GT(answer.mass, 0.0f);
    EXPECT_FLOAT_EQ(answer.position.x, 2.0f);
    EXPECT_FLOAT_EQ(answer.bounds.Center.x, 2.0f);
}

TEST(MapObjTackle, FreezePushesInAndReleaseRestoresThenLaunches)
{
    NS::Obj::Scene scene;
    NS::Obj::Actor* rock = PlaceRock(scene, NS::Core::Vector3{2.0f, 1.0f, 0.0f});
    ASSERT_NE(rock, nullptr);

    const NS::Game::Level::TackleFreezeDesc freeze{
        .impactDir = NS::Core::Vector3{1.0f, 0.0f, 0.0f}, .pushInDistance = 0.1f, .squash = true, .stopSteps = 3};
    ASSERT_TRUE(NS::Game::Level::SendMsgTackleFreeze(*rock, freeze));
    EXPECT_FLOAT_EQ(rock->Root().Position().x, 2.1f);
    const NS::Game::Level::MapObj* reaction = static_cast<NS::Game::Level::MapObj*>(rock);
    EXPECT_TRUE(reaction->IsFrozen());

    NS::Game::Level::TackleReleaseDesc release{};
    release.arc = NS::Game::Level::LaunchArc{
        .direction = NS::Core::Vector3{1.0f, 0.0f, 0.0f}, .distance = 5.0f, .apexHeight = 1.0f};
    ASSERT_TRUE(NS::Game::Level::SendMsgTackleRelease(*rock, release));
    // 食い込みは見せるための動き。明けで元の位置へ戻ってから飛ぶ
    EXPECT_FLOAT_EQ(rock->Root().Position().x, 2.0f);
    EXPECT_FALSE(reaction->IsFrozen());
    EXPECT_TRUE(reaction->IsArc());
}

TEST(MapObjTackle, FlyingRockIsNotPushedIn)
{
    NS::Obj::Scene scene;
    NS::Obj::Actor* rock = PlaceRock(scene, NS::Core::Vector3{2.0f, 1.0f, 0.0f});
    ASSERT_NE(rock, nullptr);
    NS::Game::Level::TackleReleaseDesc release;
    release.arc = NS::Game::Level::LaunchArc{
        .direction = NS::Core::Vector3{1.0f, 0.0f, 0.0f}, .distance = 5.0f, .apexHeight = 1.0f};
    ASSERT_TRUE(NS::Game::Level::SendMsgTackleRelease(*rock, release));

    NS::Game::Level::TackleTargetAnswer answer{};
    ASSERT_TRUE(NS::Game::Level::SendMsgAskTackleTarget(*rock->BodySensorPart(), answer));
    EXPECT_FALSE(answer.placed);

    const NS::Game::Level::TackleFreezeDesc freeze{
        .impactDir = NS::Core::Vector3{1.0f, 0.0f, 0.0f}, .pushInDistance = 0.1f, .stopSteps = 3};
    ASSERT_TRUE(NS::Game::Level::SendMsgTackleFreeze(*rock, freeze));
    EXPECT_FLOAT_EQ(rock->Root().Position().x, 2.0f);
}

TEST(MapObjTackle, DestructionRemainsDisabled)
{
    NS::Obj::Scene scene;
    NS::Obj::Actor* rock = PlaceRock(scene, NS::Core::Vector3{2.0f, 1.0f, 0.0f});
    ASSERT_NE(rock, nullptr);
    NS::Game::Level::TackleReleaseDesc release{};
    release.breaks = true;
    ASSERT_TRUE(NS::Game::Level::SendMsgTackleRelease(*rock, release));
    EXPECT_TRUE(rock->BodySensorPart()->IsValid());
    EXPECT_TRUE(rock->IsActiveInHierarchy());
}

TEST(MapObjTackle, ReplacingAPendingFreezeDoesNotAccumulatePushIn)
{
    NS::Obj::Scene scene;
    NS::Obj::Actor* rock = PlaceRock(scene, NS::Core::Vector3{2.0f, 1.0f, 0.0f});
    ASSERT_NE(rock, nullptr);
    NS::Game::Level::TackleFreezeDesc freeze{
        .impactDir = NS::Core::Vector3{1.0f, 0.0f, 0.0f}, .pushInDistance = 0.1f, .stopSteps = 3};
    ASSERT_TRUE(NS::Game::Level::SendMsgTackleFreeze(*rock, freeze));
    EXPECT_FLOAT_EQ(rock->Root().Position().x, 2.1f);
    freeze.pushInDistance = 0.2f;
    ASSERT_TRUE(NS::Game::Level::SendMsgTackleFreeze(*rock, freeze));
    EXPECT_FLOAT_EQ(rock->Root().Position().x, 2.2f);
    NS::Game::Level::TackleReleaseDesc release{};
    release.arc = NS::Game::Level::LaunchArc{
        .direction = NS::Core::Vector3{1.0f, 0.0f, 0.0f}, .distance = 5.0f, .apexHeight = 1.0f};
    ASSERT_TRUE(NS::Game::Level::SendMsgTackleRelease(*rock, release));
    EXPECT_FLOAT_EQ(rock->Root().Position().x, 2.0f);
    EXPECT_TRUE(static_cast<NS::Game::Level::MapObj*>(rock)->IsArc());
}
