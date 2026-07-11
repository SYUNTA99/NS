#include <gtest/gtest.h>

#include <Framework/Physics/PhysicsWorld.h>
#include <Framework/Scene/AssetManager.h>
#include <Framework/Scene/Components/PlacedVirtualCamera.h>
#include <Framework/Scene/Components/ThirdPersonFollowComponent.h>
#include <Framework/Scene/Components/VirtualCameraComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/ObjectRefSubsystem.h>
#include <Framework/Scene/SceneBase.h>
#include <Framework/Scene/SceneWorld.h>
#include <Game/Blocks/BuildPlacedObject.h>
#include <Game/Level/LevelObjects.h>
#include <Game/Player.h>

#include <filesystem>
#include <utility>
#include <vector>

using NS::Scene::SceneData;
using NS::Scene::SceneWorld;

namespace
{
    // world は型付き控えを持たないので、テストも本番の読み手と同じ問い合わせ口から集める
    template <class T> std::vector<T*> Collect(const SceneWorld& world)
    {
        std::vector<T*> result;
        world.ForEachComponent<T>([&result](T& comp) { result.push_back(&comp); });
        return result;
    }

    // 本番 LevelPlayScene と同じ組み方: ゲームのファクトリを SceneWorld へ渡す
    NS::Scene::ObjectFactoryFn MakeFactory(NS::Scene::AssetManager& assets, const SceneData& level)
    {
        return [&assets, &level](const NS::Scene::ObjectData& entry) {
            return NS::Game::Blocks::BuildPlacedObject(entry, assets, level.materialPaths);
        };
    }
} // namespace

TEST(SceneWorldTest, InitialStateIsEmpty)
{
    SceneWorld world;
    EXPECT_TRUE(world.Objects().empty());
    EXPECT_TRUE(world.SourceIndices().empty());
}

TEST(SceneWorldTest, RebuildClearsStalePhysicsAndBuildsNothingWithoutFactory)
{
    NS::Scene::SceneBase scene;
    NS::Physics::PhysicsWorld physics;
    // 古い衝突が Rebuild 後に残ると、 編集で消した block へ当たり続ける。 必ず Clear から始まる仕様を固定する
    physics.AddAabb(NS::Math::AABB{NS::Math::Vector3{0.0f, 0.0f, 0.0f}, NS::Math::Vector3{1.0f, 1.0f, 1.0f}});
    physics.BuildBroadphase();
    ASSERT_FALSE(physics.IsEmpty());

    SceneWorld world;
    const SceneData level{};
    world.Rebuild(level, scene, physics, nullptr);

    EXPECT_TRUE(physics.IsEmpty());
    EXPECT_TRUE(world.Objects().empty());
}

TEST(SceneWorldTest, ClearEmptiesEverything)
{
    SceneWorld world;
    world.Clear();
    EXPECT_TRUE(world.Objects().empty());
    EXPECT_TRUE(world.SourceIndices().empty());
}

// プレイヤー実体も他の配置物と同じ一本道で組まれ、 データが決めた id の解決で実体が引ける
TEST(SceneWorldTest, RebuildBuildsPlayerAndResolvesItById)
{
    SceneData level;
    level.objects.push_back(NS::Game::Level::MakeCellObject(0, 0, 0, 0));
    level.objects.push_back(
        NS::Game::Level::MakePlayerObject(NS::Math::Vector3{0.0f, 1.41f, 0.0f}, NS::Math::Quaternion{}));
    NS::Scene::EnsureUniqueObjectIds(level);

    NS::Scene::SceneBase scene;
    scene.CreateSceneSubsystems();
    NS::Physics::PhysicsWorld physics;
    NS::Scene::AssetManager assets{std::filesystem::path{"."}};
    SceneWorld world;
    world.Rebuild(level, scene, physics, MakeFactory(assets, level));

    // grid block とプレイヤーの両方が組まれ、 id 解決は所有リスト内の実体を指す
    ASSERT_EQ(world.Objects().size(), 2u);
    auto* refs = scene.GetSubsystem<NS::Scene::ObjectRefSubsystem>();
    ASSERT_NE(refs, nullptr);
    const std::size_t playerIndex = NS::Game::Level::FindPlayerObjectIndex(level);
    ASSERT_NE(playerIndex, NS::Scene::kNoObjectIndex);
    NS::Scene::GameObject* resolved = refs->Resolve(NS::Scene::ObjectRef{level.objects[playerIndex].objectId});
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved, world.Objects()[1].get());
    EXPECT_FLOAT_EQ(resolved->Root().Position().y, 1.41f);
}

// 追従カメラの配置物は Rebuild 後も休止のまま問い合わせで引け、Target 参照が実体へ解決される
// 参照先より前に並ぶ前方参照でも、組み立てを先に済ませてから開始する二段組みで解決できる
TEST(SceneWorldTest, RebuildBakesFollowCameraAndResolvesTarget)
{
    SceneData level;
    level.objects.push_back(NS::Game::Level::MakeFollowCameraObject(0u));
    level.objects.push_back(NS::Game::Level::MakeCellObject(0, 0, 0, 0));
    NS::Scene::EnsureUniqueObjectIds(level);
    // 追従先は自分より後ろに並ぶ grid block。Target 参照を採番後の実 id へ差し替える
    for (auto& component : level.objects[0].components)
        for (auto& field : component.fields)
            if (field.name == "Target")
                field.value = NS::Scene::ObjectRef{level.objects[1].objectId};

    NS::Scene::SceneBase scene;
    scene.CreateSceneSubsystems();
    NS::Physics::PhysicsWorld physics;
    NS::Scene::AssetManager assets{std::filesystem::path{"."}};
    SceneWorld world;
    world.Rebuild(level, scene, physics, MakeFactory(assets, level));

    const auto follows = Collect<NS::Scene::ThirdPersonFollowComponent>(world);
    ASSERT_EQ(follows.size(), 1u);
    auto* follow = follows[0];
    EXPECT_FALSE(follow->IsActive());
    // Brain 登録が使う抽象基底の問い合わせでも同じ実体が引ける
    const auto vcams = Collect<NS::Scene::VirtualCameraComponent>(world);
    ASSERT_EQ(vcams.size(), 1u);
    EXPECT_EQ(vcams[0], follow);
    // データの Far Plane 100 が反射 set で効いている
    EXPECT_FLOAT_EQ(follow->FarPlane(), 100.0f);

    // 追従先の解決: world が組んだ grid block の Root を指す
    ASSERT_EQ(world.Objects().size(), 2u);
    EXPECT_EQ(follow->Target(), &world.Objects()[1]->Root());
}

// 据え置きカメラの配置物は問い合わせで引け、エリア外の非アクティブで組み上がる
TEST(SceneWorldTest, RebuildBakesPlacedCamerasInactive)
{
    SceneData level;
    NS::Scene::ObjectData cameraObject{};
    cameraObject.positionX = 8.0f;
    NS::Scene::ComponentData comp;
    comp.typeName = "PlacedVirtualCamera";
    comp.fields.push_back(NS::Scene::FieldValue{"Priority", 20});
    cameraObject.components.push_back(std::move(comp));
    level.objects.push_back(std::move(cameraObject));

    NS::Scene::SceneBase scene;
    NS::Physics::PhysicsWorld physics;
    NS::Scene::AssetManager assets{std::filesystem::path{"."}};
    SceneWorld world;
    world.Rebuild(level, scene, physics, MakeFactory(assets, level));

    const auto placedCameras = Collect<NS::Scene::PlacedVirtualCamera>(world);
    ASSERT_EQ(placedCameras.size(), 1u);
    auto* placed = placedCameras[0];
    EXPECT_FALSE(placed->IsActive());
    EXPECT_EQ(placed->VcamPriority(), 20);
    // 視点位置は object の Transform から来る
    EXPECT_FLOAT_EQ(placed->ViewPosition().x, 8.0f);
}
