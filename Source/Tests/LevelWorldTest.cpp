#include <gtest/gtest.h>

#include <Framework/Physics/PhysicsWorld.h>
#include <Framework/Scene/AssetManager.h>
#include <Framework/Scene/Components/PlacedVirtualCamera.h>
#include <Framework/Scene/Components/ThirdPersonFollowComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/ObjectRefSubsystem.h>
#include <Framework/Scene/SceneBase.h>
#include <GameCore/Level/LevelData.h>
#include <GameCore/Level/LevelWorld.h>
#include <GameCore/Player.h>

#include <filesystem>
#include <utility>

using NS::GameCore::Level::LevelData;
using NS::GameCore::Level::LevelWorld;

TEST(LevelWorldTest, InitialStateIsEmpty)
{
    LevelWorld world;
    EXPECT_TRUE(world.Objects().empty());
    EXPECT_TRUE(world.SourceIndices().empty());
    EXPECT_TRUE(world.HazardView().empty());
    EXPECT_TRUE(world.LedgeEdges().empty());
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
    EXPECT_TRUE(world.HazardView().empty());
    EXPECT_TRUE(world.PlacedCameras().empty());
    EXPECT_TRUE(world.FollowCameras().empty());
    EXPECT_TRUE(world.VirtualCameras().empty());
}

// プレイヤー実体も他の配置物と同じ一本道で組まれ、 型付き view から引ける
TEST(LevelWorldTest, RebuildBuildsPlayerAndExposesView)
{
    LevelData level;
    level.objects.push_back(NS::GameCore::Level::MakeCellObject(0, 0, 0, 0));
    level.objects.push_back(
        NS::GameCore::Level::MakePlayerObject(NS::Math::Vector3{0.0f, 1.41f, 0.0f}, NS::Math::Quaternion{}));

    NS::Scene::SceneBase scene;
    NS::Physics::PhysicsWorld physics;
    NS::Scene::AssetManager assets{std::filesystem::path{"."}};
    LevelWorld world;
    world.Rebuild(level, scene, physics, &assets);

    // grid block とプレイヤーの両方が組まれ、 view は所有リスト内の実体を指す
    ASSERT_EQ(world.Objects().size(), 2u);
    ASSERT_NE(world.PlayerView(), nullptr);
    EXPECT_EQ(world.PlayerView(), dynamic_cast<Player*>(world.Objects()[1].get()));
    EXPECT_FLOAT_EQ(world.PlayerView()->Root().Position().y, 1.41f);

    world.Clear();
    EXPECT_EQ(world.PlayerView(), nullptr);
}

// 追従カメラの配置物は Rebuild で休止のまま走査 view に載り、Target 参照が実体へ解決される
// 参照先より前に並ぶ前方参照でも、組み立てを先に済ませてから開始する二段組みで解決できる
TEST(LevelWorldTest, RebuildBakesFollowCameraAndResolvesTarget)
{
    LevelData level;
    level.objects.push_back(NS::GameCore::Level::MakeFollowCameraObject(0u));
    level.objects.push_back(NS::GameCore::Level::MakeCellObject(0, 0, 0, 0));
    NS::GameCore::Level::EnsureUniqueObjectIds(level);
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
    NS::GameCore::Level::ObjectInstance cameraObject{};
    cameraObject.positionX = 8.0f;
    NS::GameCore::Level::ComponentData comp;
    comp.typeName = "PlacedVirtualCamera";
    comp.fields.push_back(NS::GameCore::Level::FieldValue{"Priority", 20});
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
