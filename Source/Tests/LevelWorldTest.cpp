#include <gtest/gtest.h>

#include <Framework/Physics/PhysicsWorld.h>
#include <Framework/Scene/SceneBase.h>
#include <Game/Level/LevelData.h>
#include <Game/Level/LevelWorld.h>

using NS::Game::Level::LevelData;
using NS::Game::Level::LevelWorld;

TEST(LevelWorldTest, InitialStateIsEmpty)
{
    LevelWorld world;
    EXPECT_TRUE(world.Objects().empty());
    EXPECT_TRUE(world.SourceIndices().empty());
    EXPECT_TRUE(world.InstancedBlocks().empty());
    EXPECT_TRUE(world.HazardView().empty());
    EXPECT_TRUE(world.LedgeEdges().empty());
    EXPECT_EQ(world.Batcher(), nullptr);
}

TEST(LevelWorldTest, RebuildClearsStalePhysicsAndBuildsNothingWithoutAssets)
{
    NS::Scene::SceneBase scene;
    NS::Physics::PhysicsWorld physics;
    // 古い衝突が Rebuild 後に残ると、 編集で消した block へ当たり続ける。 必ず Clear から始まる仕様を固定する
    physics.AddAabb(NS::Math::AABB{NS::Math::Vector3{0.0f, 0.0f, 0.0f}, NS::Math::Vector3{1.0f, 1.0f, 1.0f}});
    physics.BuildBroadphase();
    ASSERT_FALSE(physics.IsEmpty());

    LevelWorld world;
    const LevelData level{};
    world.Rebuild(level, scene, physics, nullptr);

    EXPECT_TRUE(physics.IsEmpty());
    EXPECT_TRUE(world.Objects().empty());
    EXPECT_TRUE(world.HazardView().empty());
}

TEST(LevelWorldTest, ClearEmptiesEverything)
{
    LevelWorld world;
    world.Clear();
    EXPECT_TRUE(world.Objects().empty());
    EXPECT_TRUE(world.SourceIndices().empty());
    EXPECT_TRUE(world.InstancedBlocks().empty());
    EXPECT_TRUE(world.HazardView().empty());
}
