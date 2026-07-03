#include <gtest/gtest.h>

#include <Framework/Physics/PhysicsWorld.h>
#include <Framework/Scene/AssetManager.h>
#include <Framework/Scene/Components/PlacedVirtualCamera.h>
#include <Framework/Scene/Components/ThirdPersonFollowComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/ObjectRefSubsystem.h>
#include <Framework/Scene/SceneBase.h>
#include <Game/Level/LevelData.h>
#include <Game/Level/LevelWorld.h>

#include <filesystem>
#include <utility>

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
    EXPECT_TRUE(world.PlacedCameras().empty());
    EXPECT_TRUE(world.FollowCameras().empty());
    EXPECT_TRUE(world.VirtualCameras().empty());
}

// プレイヤー実体は scene 所有の実 player が演じるため、Rebuild は runtime GameObject を組まない
TEST(LevelWorldTest, RebuildSkipsPlayerObject)
{
    LevelData level;
    level.objects.push_back(NS::Game::Level::MakeGridObject(0, 0, 0, 0));
    level.objects.push_back(
        NS::Game::Level::MakePlayerObject(NS::Math::Vector3{0.0f, 1.41f, 0.0f}, NS::Math::Quaternion{}));

    NS::Scene::SceneBase scene;
    NS::Physics::PhysicsWorld physics;
    NS::Scene::AssetManager assets{std::filesystem::path{"."}};
    LevelWorld world;
    world.Rebuild(level, scene, physics, &assets);

    // grid block 1 個だけが組まれ、player object の添字は world に現れない
    ASSERT_EQ(world.Objects().size(), 1u);
    ASSERT_EQ(world.SourceIndices().size(), 1u);
    EXPECT_EQ(world.SourceIndices()[0], 0u);
}

// 追従カメラの配置物は Rebuild で休止のまま走査 view に載り、Target 参照が実体へ解決される
// 参照先より前に並ぶ前方参照でも、組み立てを先に済ませてから開始する二段組みで解決できる
TEST(LevelWorldTest, RebuildBakesFollowCameraAndResolvesTarget)
{
    LevelData level;
    level.objects.push_back(NS::Game::Level::MakeFollowCameraObject(0u));
    level.objects.push_back(NS::Game::Level::MakeGridObject(0, 0, 0, 0));
    NS::Game::Level::EnsureUniqueObjectIds(level);
    // 追従先は自分より後ろに並ぶ grid block。Target 参照を採番後の実 id へ差し替える
    for (auto& component : level.objects[0].components)
        for (auto& field : component.fields)
            if (field.name == "Target")
                field.value = NS::Scene::ObjectRef{level.objects[1].objectId};

    NS::Scene::SceneBase scene;
    scene.CreateSceneSubsystems();
    NS::Physics::PhysicsWorld physics;
    NS::Scene::AssetManager assets{std::filesystem::path{"."}};
    LevelWorld world;
    world.Rebuild(level, scene, physics, &assets);

    ASSERT_EQ(world.FollowCameras().size(), 1u);
    auto* follow = world.FollowCameras()[0];
    EXPECT_FALSE(follow->IsActive());
    // Brain 登録用の束ね view にも同じ実体が載る
    ASSERT_EQ(world.VirtualCameras().size(), 1u);
    EXPECT_EQ(world.VirtualCameras()[0], follow);
    // データの Far Plane 100 が反射 set で効いている
    EXPECT_FLOAT_EQ(follow->FarPlane(), 100.0f);

    // 追従先の解決: world が組んだ grid block の Root を指す
    ASSERT_EQ(world.Objects().size(), 2u);
    EXPECT_EQ(follow->Target(), &world.Objects()[1]->Root());
}

// 据え置きカメラの配置物は Rebuild で走査 view に載り、エリア外の非アクティブで組み上がる
TEST(LevelWorldTest, RebuildBakesPlacedCamerasInactive)
{
    LevelData level;
    NS::Game::Level::ObjectInstance cameraObject{};
    cameraObject.positionX = 8.0f;
    NS::Game::Level::ComponentData comp;
    comp.typeName = "PlacedVirtualCamera";
    comp.fields.push_back(NS::Game::Level::FieldValue{"Priority", 20});
    cameraObject.components.push_back(std::move(comp));
    level.objects.push_back(std::move(cameraObject));

    NS::Scene::SceneBase scene;
    NS::Physics::PhysicsWorld physics;
    NS::Scene::AssetManager assets{std::filesystem::path{"."}};
    LevelWorld world;
    world.Rebuild(level, scene, physics, &assets);

    ASSERT_EQ(world.PlacedCameras().size(), 1u);
    auto* placed = world.PlacedCameras()[0];
    EXPECT_FALSE(placed->IsActive());
    EXPECT_EQ(placed->VcamPriority(), 20);
    // 視点位置は object の Transform から来る
    EXPECT_FLOAT_EQ(placed->ViewPosition().x, 8.0f);
}
