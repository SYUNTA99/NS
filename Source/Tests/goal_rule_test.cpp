#include "Game/Player.h"

#include <Game/Level/GoalComponent.h>
#include <gtest/gtest.h>
#include <Runtime/Object/Components/TransformComponent.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Scene/Scene.h>
#include <utility>

namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Object;

//! ゴール配置物が LateUpdate 帯で自分から接触を判定し、フラグを保持することを検証する

namespace
{
    SceneNs::ObjectData MakeGoal(float x, float y, float z)
    {
        SceneNs::ObjectData object;
        SceneNs::SetObjectPosition(object, NS::Core::Vector3{x, y, z});
        object.components.push_back(SceneNs::MakeComponentEntry("GoalComponent"));
        return object;
    }

    LevelNs::GoalComponent* FindGoal(SceneNs::World& world)
    {
        LevelNs::GoalComponent* found = nullptr;
        world.ForEachComponent<LevelNs::GoalComponent>([&](LevelNs::GoalComponent& goal) { found = &goal; });
        return found;
    }
} // namespace

TEST(GoalTest, ReachedWhenPlayerWithinRadius)
{
    SceneNs::Scene scene;
    SceneNs::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    data.objects.push_back(MakeGoal(0.0f, 0.0f, 0.0f));
    scene.LoadFromData(std::move(data));

    auto* goal = FindGoal(scene.World());
    ASSERT_NE(goal, nullptr);
    EXPECT_FALSE(goal->Reached());

    scene.World().UpdateObjects(SceneNs::TickPriority::LateUpdate);

    EXPECT_TRUE(goal->Reached());
}

TEST(GoalTest, NotReachedWhenFar)
{
    SceneNs::Scene scene;
    SceneNs::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    data.objects.push_back(MakeGoal(10.0f, 0.0f, 0.0f));
    scene.LoadFromData(std::move(data));

    scene.World().UpdateObjects(SceneNs::TickPriority::LateUpdate);

    auto* goal = FindGoal(scene.World());
    ASSERT_NE(goal, nullptr);
    EXPECT_FALSE(goal->Reached());
}

TEST(GoalTest, ReachedLatchesUntilReset)
{
    SceneNs::Scene scene;
    SceneNs::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    data.objects.push_back(MakeGoal(0.0f, 0.0f, 0.0f));
    scene.LoadFromData(std::move(data));

    auto* goal = FindGoal(scene.World());
    ASSERT_NE(goal, nullptr);
    scene.World().UpdateObjects(SceneNs::TickPriority::LateUpdate);
    ASSERT_TRUE(goal->Reached());

    // 触れた後に離れてもフラグは立ったまま
    auto* player = FindPlayer(scene.World());
    ASSERT_NE(player, nullptr);
    player->Root().SetPosition(NS::Core::Vector3{10.0f, 0.0f, 0.0f});
    scene.World().UpdateObjects(SceneNs::TickPriority::LateUpdate);
    EXPECT_TRUE(goal->Reached());

    // 戻すのは respawner のやり直し。戻した後は離れている限り立たない
    goal->ResetReached();
    scene.World().UpdateObjects(SceneNs::TickPriority::LateUpdate);
    EXPECT_FALSE(goal->Reached());
}
