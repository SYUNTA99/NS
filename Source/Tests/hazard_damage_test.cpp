#include "Game/Level/BlockObject.h"
#include "Game/Player.h"

#include <Game/Level/HealthComponent.h>
#include <gtest/gtest.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Scene/Scene.h>
#include <utility>

namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Object;

//! 命の増減の能力と、hazard 配置物が LateUpdate 帯で自分から削ってくる自走を検証する

TEST(HealthTest, StartsFullAt8)
{
    SceneNs::GameObject owner;
    auto* health = owner.AddComponent<LevelNs::HealthComponent>();

    EXPECT_EQ(health->Current(), 8);
    EXPECT_FALSE(health->IsDead());
}

TEST(HealthTest, ApplyDamageDecrements)
{
    SceneNs::GameObject owner;
    auto* health = owner.AddComponent<LevelNs::HealthComponent>();

    health->ApplyDamage(1);

    EXPECT_EQ(health->Current(), 7);
    EXPECT_FALSE(health->IsDead());
}

TEST(HealthTest, DamageClampsAtZeroAndFlagsDead)
{
    SceneNs::GameObject owner;
    auto* health = owner.AddComponent<LevelNs::HealthComponent>();

    for (int i = 0; i < 10; ++i)
        health->ApplyDamage(1);

    EXPECT_EQ(health->Current(), 0);
    EXPECT_TRUE(health->IsDead());
}

TEST(HealthTest, KillDropsToZero)
{
    SceneNs::GameObject owner;
    auto* health = owner.AddComponent<LevelNs::HealthComponent>();

    health->Kill();

    EXPECT_EQ(health->Current(), 0);
    EXPECT_TRUE(health->IsDead());
}

TEST(HealthTest, ResetRestoresFull)
{
    SceneNs::GameObject owner;
    auto* health = owner.AddComponent<LevelNs::HealthComponent>();

    health->Kill();
    health->Reset();

    EXPECT_EQ(health->Current(), 8);
    EXPECT_FALSE(health->IsDead());
}

TEST(HazardTest, DrainsPlayerHealthThroughLateUpdateBand)
{
    SceneNs::Scene scene;
    SceneNs::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    // プレイヤーと同じ位置の cell に hazard の印を足すと、カプセルと箱が必ず重なる
    SceneNs::ObjectData hazard = LevelNs::MakeCellObject(0, 0, 0);
    hazard.components.push_back(SceneNs::MakeComponentEntry("HazardComponent"));
    data.objects.push_back(hazard);
    scene.LoadFromData(std::move(data));

    auto* player = FindPlayer(scene.World());
    ASSERT_NE(player, nullptr);

    // LateUpdate 帯が回るたび、hazard が自分で重なりを判定して 1 ずつ削る
    scene.World().UpdateObjects(SceneNs::TickPriority::LateUpdate);
    EXPECT_EQ(player->Health(), 7);
    scene.World().UpdateObjects(SceneNs::TickPriority::LateUpdate);
    EXPECT_EQ(player->Health(), 6);
}

TEST(HazardTest, NoOverlapNoDamage)
{
    SceneNs::Scene scene;
    SceneNs::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    SceneNs::ObjectData hazard = LevelNs::MakeCellObject(10, 0, 0);
    hazard.components.push_back(SceneNs::MakeComponentEntry("HazardComponent"));
    data.objects.push_back(hazard);
    scene.LoadFromData(std::move(data));

    auto* player = FindPlayer(scene.World());
    ASSERT_NE(player, nullptr);

    scene.World().UpdateObjects(SceneNs::TickPriority::LateUpdate);

    EXPECT_EQ(player->Health(), 8);
}
