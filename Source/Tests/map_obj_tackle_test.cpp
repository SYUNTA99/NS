#include "Game/Level/LaunchArc.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/MapObj.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/SubObjects/HitSensor.h"
#include "NSlib/Object/SubObjects/TransformSubObject.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Scene/SceneJson.h"

#include <gtest/gtest.h>

// 体当たりを受けた置物が、知らせだけで答え・食い込み・戻り・飛ぶことを縛る

namespace
{
    NS::Obj::Actor* PlaceRock(NS::Obj::Scene& scene, const NS::Vector3& position)
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
    NS::Obj::Actor* rock = PlaceRock(scene, NS::Vector3{2.0f, 1.0f, 0.0f});
    ASSERT_NE(rock, nullptr);
    NS::Obj::HitSensor* body = rock->BodySensorSubObj();
    ASSERT_NE(body, nullptr);

    GL::Level::TackleTargetAnswer answer{};
    ASSERT_TRUE(GL::Level::SendMsgAskTackleTarget(*body, answer));
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
    NS::Obj::Actor* rock = PlaceRock(scene, NS::Vector3{2.0f, 1.0f, 0.0f});
    ASSERT_NE(rock, nullptr);

    const GL::Level::TackleFreezeDesc freeze{
        .impactDir = NS::Vector3{1.0f, 0.0f, 0.0f}, .pushInDistance = 0.1f, .squash = true, .stopSteps = 3};
    ASSERT_TRUE(GL::Level::SendMsgTackleFreeze(*rock, freeze));
    EXPECT_FLOAT_EQ(rock->Root().Position().x, 2.1f);
    const GL::Level::MapObj* reaction = static_cast<GL::Level::MapObj*>(rock);
    EXPECT_TRUE(reaction->IsFrozen());

    GL::Level::TackleReleaseDesc release{};
    release.arc = GL::Level::LaunchArc{
        .direction = NS::Vector3{1.0f, 0.0f, 0.0f}, .distance = 5.0f, .apexHeight = 1.0f};
    ASSERT_TRUE(GL::Level::SendMsgTackleRelease(*rock, release));
    // 食い込みは見せるための動き。明けで元の位置へ戻ってから飛ぶ
    EXPECT_FLOAT_EQ(rock->Root().Position().x, 2.0f);
    EXPECT_FALSE(reaction->IsFrozen());
    EXPECT_TRUE(reaction->IsArc());
}

TEST(MapObjTackle, FlyingRockIsNotPushedIn)
{
    NS::Obj::Scene scene;
    NS::Obj::Actor* rock = PlaceRock(scene, NS::Vector3{2.0f, 1.0f, 0.0f});
    ASSERT_NE(rock, nullptr);
    GL::Level::TackleReleaseDesc release;
    release.arc = GL::Level::LaunchArc{
        .direction = NS::Vector3{1.0f, 0.0f, 0.0f}, .distance = 5.0f, .apexHeight = 1.0f};
    ASSERT_TRUE(GL::Level::SendMsgTackleRelease(*rock, release));

    GL::Level::TackleTargetAnswer answer{};
    ASSERT_TRUE(GL::Level::SendMsgAskTackleTarget(*rock->BodySensorSubObj(), answer));
    EXPECT_FALSE(answer.placed);

    const GL::Level::TackleFreezeDesc freeze{
        .impactDir = NS::Vector3{1.0f, 0.0f, 0.0f}, .pushInDistance = 0.1f, .stopSteps = 3};
    ASSERT_TRUE(GL::Level::SendMsgTackleFreeze(*rock, freeze));
    EXPECT_FLOAT_EQ(rock->Root().Position().x, 2.0f);
}

TEST(MapObjTackle, DestructionRemainsDisabled)
{
    NS::Obj::Scene scene;
    NS::Obj::Actor* rock = PlaceRock(scene, NS::Vector3{2.0f, 1.0f, 0.0f});
    ASSERT_NE(rock, nullptr);
    GL::Level::TackleReleaseDesc release{};
    release.breaks = true;
    ASSERT_TRUE(GL::Level::SendMsgTackleRelease(*rock, release));
    EXPECT_TRUE(rock->BodySensorSubObj()->IsValid());
    EXPECT_TRUE(rock->IsActiveInHierarchy());
}

TEST(MapObjTackle, ReplacingAPendingFreezeDoesNotAccumulatePushIn)
{
    NS::Obj::Scene scene;
    NS::Obj::Actor* rock = PlaceRock(scene, NS::Vector3{2.0f, 1.0f, 0.0f});
    ASSERT_NE(rock, nullptr);
    GL::Level::TackleFreezeDesc freeze{
        .impactDir = NS::Vector3{1.0f, 0.0f, 0.0f}, .pushInDistance = 0.1f, .stopSteps = 3};
    ASSERT_TRUE(GL::Level::SendMsgTackleFreeze(*rock, freeze));
    EXPECT_FLOAT_EQ(rock->Root().Position().x, 2.1f);
    freeze.pushInDistance = 0.2f;
    ASSERT_TRUE(GL::Level::SendMsgTackleFreeze(*rock, freeze));
    EXPECT_FLOAT_EQ(rock->Root().Position().x, 2.2f);
    GL::Level::TackleReleaseDesc release{};
    release.arc = GL::Level::LaunchArc{
        .direction = NS::Vector3{1.0f, 0.0f, 0.0f}, .distance = 5.0f, .apexHeight = 1.0f};
    ASSERT_TRUE(GL::Level::SendMsgTackleRelease(*rock, release));
    EXPECT_FLOAT_EQ(rock->Root().Position().x, 2.0f);
    EXPECT_TRUE(static_cast<GL::Level::MapObj*>(rock)->IsArc());
}
