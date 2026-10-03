#include "Game/Level/CourseDirector.h"
#include "Game/Level/Goal.h"
#include "Game/Level/KillZone.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Player.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/IUse/IUseSceneObj.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Scene/SceneJson.h"
#include "Runtime/Object/ScreenFade.h"

#include <gtest/gtest.h>

// 落下死とゴールがメッセージで伝わり、コースの進行役が流れを進めることを縛る

namespace
{
    // プレイヤーと落下死の範囲を敷いたシーン文書
    nlohmann::json CourseWithPlayer()
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        (void)EnsurePlayerObject(doc);
        (void)NS::Game::Level::EnsureKillZoneObject(doc);
        return doc;
    }

    NS::Core::Vector3 SpawnOf(const nlohmann::json& doc)
    {
        return NS::Obj::ObjectPosition(NS::Obj::SceneJsonObjects(doc)[FindPlayerObjectIndex(doc)]);
    }
} // namespace

TEST(CourseDirector, PlayerCreatesDirectorAfterPlacement)
{
    NS::Obj::Scene scene;
    scene.LoadJson(CourseWithPlayer());
    EXPECT_NE(NS::Obj::FindSceneObj<NS::Game::Level::CourseDirector>(scene), nullptr);

    // 組み直すと捨てられ、プレイヤーの居ないシーンでは作られない
    scene.LoadJson(NS::Obj::MakeSceneJson());
    EXPECT_EQ(NS::Obj::FindSceneObj<NS::Game::Level::CourseDirector>(scene), nullptr);
}

TEST(CourseDirector, KillZoneKillsPlayerAndDirectorRestartsCourse)
{
    const nlohmann::json doc = CourseWithPlayer();
    NS::Obj::Scene scene;
    scene.LoadJson(doc);
    (void)scene.BeginPlayBaseline();
    Player* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);

    // 落下死の範囲 (上面 y=-50) の中へ落とす
    player->Root().SetPosition(NS::Core::Vector3{0.0f, -52.0f, 0.0f});
    scene.HitSensors().OnTick();
    EXPECT_TRUE(player->IsDead());

    NS::Game::Level::CourseDirector* director = NS::Obj::FindSceneObj<NS::Game::Level::CourseDirector>(scene);
    ASSERT_NE(director, nullptr);
    director->OnTick();
    // 出現位置へ戻り、命も満タンへ戻る
    EXPECT_FALSE(player->IsDead());
    EXPECT_FLOAT_EQ(player->Root().Position().y, SpawnOf(doc).y);
}

TEST(CourseDirector, GoalStartsClearSequenceAndLocksInput)
{
    nlohmann::json doc = CourseWithPlayer();
    nlohmann::json goal = NS::Obj::MakePrototypeJson<NS::Game::Level::Goal>();
    NS::Obj::SetObjectPosition(goal, SpawnOf(doc));
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(goal));
    NS::Obj::EnsureUniqueObjectIds(doc);

    NS::Obj::Scene scene;
    scene.LoadJson(doc);
    (void)scene.BeginPlayBaseline();
    Player* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    NS::Game::Level::CourseDirector* director = NS::Obj::FindSceneObj<NS::Game::Level::CourseDirector>(scene);
    ASSERT_NE(director, nullptr);

    scene.HitSensors().OnTick();
    director->OnTick();
    EXPECT_TRUE(director->IsClearing());
    EXPECT_TRUE(director->Fade().IsFading());
    const NS::Obj::PlayerInput* input = NS::Obj::ComponentCast<NS::Obj::PlayerInput>(player->Part("Input"));
    ASSERT_NE(input, nullptr);
    EXPECT_FALSE(input->IsActiveSelf());

    // 流れの最中に届くゴールの知らせは捨てる
    scene.HitSensors().OnTick();
    director->OnTick();
    EXPECT_TRUE(director->IsClearing());
}

TEST(CourseDirector, RestartReturnsObjectsToBaseline)
{
    nlohmann::json doc = CourseWithPlayer();
    nlohmann::json rock = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(rock, "MapObj");
    NS::Obj::SetObjectPosition(rock, NS::Core::Vector3{5.0f, 1.0f, 0.0f});
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
    NS::Obj::EnsureUniqueObjectIds(doc);

    NS::Obj::Scene scene;
    scene.LoadJson(doc);
    (void)scene.BeginPlayBaseline();
    NS::Obj::Actor* placed = nullptr;
    for (NS::Obj::Actor* actor : scene.Objects())
    {
        if (std::string_view{actor->ClassName()} == "MapObj")
        {
            placed = actor;
        }
    }
    ASSERT_NE(placed, nullptr);
    placed->Root().SetPosition(NS::Core::Vector3{9.0f, 1.0f, 0.0f});

    NS::Obj::GetOrCreateSceneObj<NS::Game::Level::CourseDirector>(scene)->RestartCourse();
    EXPECT_FLOAT_EQ(placed->Root().Position().x, 5.0f);
}
