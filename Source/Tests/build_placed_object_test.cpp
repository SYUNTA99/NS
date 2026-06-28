#include <gtest/gtest.h>

#include <Framework/Scene/AssetManager.h>
#include <Framework/Scene/Components/BoxColliderComponent.h>
#include <Framework/Scene/Components/CapsuleColliderComponent.h>
#include <Framework/Scene/Components/MeshRendererComponent.h>
#include <Framework/Scene/Components/SphereColliderComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Game/Blocks/BuildPlacedObject.h>
#include <Game/Level/LevelData.h>

#include <filesystem>
#include <vector>

namespace
{
    using NS::Game::Blocks::BuildPlacedObject;
    using NS::Game::Blocks::FindComponent;
    using NS::Game::Blocks::MakeFreeCubeComponents;
    using NS::Game::Level::MakeGridObject;
    using NS::Game::Level::ObjectInstance;
    using NS::Game::Level::SetObjectShapeCollider;
    using NS::Game::Level::ShapeCollider;
    using NS::Math::Vector3;

    // device を確立しない AssetManager。 Builtin / SharedMaterial は nullptr を返すが、 ファクトリは落ちない
    class BuildPlacedObjectTest : public ::testing::Test
    {
    protected:
        NS::Scene::AssetManager m_assets{std::filesystem::path{"."}};
        std::vector<std::string> m_materialPaths;

        std::unique_ptr<NS::Scene::GameObject> Build(const ObjectInstance& object)
        {
            return BuildPlacedObject(object, m_assets, m_materialPaths);
        }
    };

    // grid に置く素の cube
    ObjectInstance MakeGridCube()
    {
        return MakeGridObject(0, 0, 0, 0);
    }

    // 自由配置の cube。 当たり寸法 / offset は component を起こす前に焼く
    ObjectInstance MakeFreeCube(ShapeCollider shape,
                                const Vector3& half = Vector3{0.5f, 0.5f, 0.5f},
                                const Vector3& offset = Vector3{0.0f, 0.0f, 0.0f})
    {
        ObjectInstance object;
        object.flags = 0;
        SetObjectShapeCollider(object, shape);
        object.colliderHalfExtentsX = half.x;
        object.colliderHalfExtentsY = half.y;
        object.colliderHalfExtentsZ = half.z;
        object.colliderOffsetX = offset.x;
        object.colliderOffsetY = offset.y;
        object.colliderOffsetZ = offset.z;
        object.components = MakeFreeCubeComponents(object);
        return object;
    }

    template <class T> bool Has(NS::Scene::GameObject& obj)
    {
        return FindComponent<T>(obj) != nullptr;
    }
} // namespace

TEST_F(BuildPlacedObjectTest, GridCubeHasMeshAndBoxCollider)
{
    auto obj = Build(MakeGridCube());
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(Has<NS::Scene::MeshRendererComponent>(*obj));
    EXPECT_TRUE(Has<NS::Scene::BoxColliderComponent>(*obj));
    EXPECT_FALSE(Has<NS::Scene::SphereColliderComponent>(*obj));
    EXPECT_FALSE(Has<NS::Scene::CapsuleColliderComponent>(*obj));
}

TEST_F(BuildPlacedObjectTest, FreeBoxHasBoxColliderWithSavedHalfExtents)
{
    auto obj = Build(MakeFreeCube(ShapeCollider::Box, Vector3{1.0f, 2.0f, 3.0f}));
    ASSERT_NE(obj, nullptr);
    auto* box = FindComponent<NS::Scene::BoxColliderComponent>(*obj);
    ASSERT_NE(box, nullptr);
    const Vector3 half = box->HalfExtents();
    EXPECT_FLOAT_EQ(half.x, 1.0f);
    EXPECT_FLOAT_EQ(half.y, 2.0f);
    EXPECT_FLOAT_EQ(half.z, 3.0f);
    EXPECT_FALSE(Has<NS::Scene::SphereColliderComponent>(*obj));
    EXPECT_FALSE(Has<NS::Scene::CapsuleColliderComponent>(*obj));
}

TEST_F(BuildPlacedObjectTest, FreeSphereHasOnlySphereCollider)
{
    auto obj = Build(MakeFreeCube(ShapeCollider::Sphere, Vector3{0.7f, 0.5f, 0.5f}, Vector3{0.0f, 1.0f, 0.0f}));
    ASSERT_NE(obj, nullptr);
    EXPECT_FALSE(Has<NS::Scene::BoxColliderComponent>(*obj));
    auto* sphere = FindComponent<NS::Scene::SphereColliderComponent>(*obj);
    ASSERT_NE(sphere, nullptr);
    EXPECT_FALSE(Has<NS::Scene::CapsuleColliderComponent>(*obj));

    const NS::Physics::Sphere world = sphere->WorldSphere();
    EXPECT_NEAR(world.radius, 0.7f, 1e-4f);
    EXPECT_NEAR(world.center.y, 1.0f, 1e-4f);
}

TEST_F(BuildPlacedObjectTest, SphereColliderWorldAabbEnclosesSphere)
{
    auto obj = Build(MakeFreeCube(ShapeCollider::Sphere, Vector3{0.7f, 0.5f, 0.5f}, Vector3{0.0f, 1.0f, 0.0f}));
    ASSERT_NE(obj, nullptr);
    auto* sphere = FindComponent<NS::Scene::SphereColliderComponent>(*obj);
    ASSERT_NE(sphere, nullptr);
    const NS::Math::AABB aabb = sphere->WorldAABB();
    EXPECT_NEAR(aabb.Center.y, 1.0f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.x, 0.7f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.y, 0.7f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.z, 0.7f, 1e-4f);
}

TEST_F(BuildPlacedObjectTest, FreeCapsuleHasOnlyCapsuleCollider)
{
    auto obj = Build(MakeFreeCube(ShapeCollider::Capsule, Vector3{0.4f, 0.9f, 0.5f}));
    ASSERT_NE(obj, nullptr);
    EXPECT_FALSE(Has<NS::Scene::BoxColliderComponent>(*obj));
    auto* capsule = FindComponent<NS::Scene::CapsuleColliderComponent>(*obj);
    ASSERT_NE(capsule, nullptr);
    EXPECT_FALSE(Has<NS::Scene::SphereColliderComponent>(*obj));

    const NS::Physics::Capsule world = capsule->WorldCapsule();
    EXPECT_NEAR(world.radius, 0.4f, 1e-4f);
    EXPECT_NEAR(world.halfHeight, 0.9f, 1e-4f);
}

TEST_F(BuildPlacedObjectTest, CapsuleColliderWorldAabbEnclosesCapsule)
{
    auto obj = Build(MakeFreeCube(ShapeCollider::Capsule, Vector3{0.4f, 0.9f, 0.5f}));
    ASSERT_NE(obj, nullptr);
    auto* capsule = FindComponent<NS::Scene::CapsuleColliderComponent>(*obj);
    ASSERT_NE(capsule, nullptr);
    const NS::Math::AABB aabb = capsule->WorldAABB();
    EXPECT_NEAR(aabb.Extents.x, 0.4f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.y, 1.3f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.z, 0.4f, 1e-4f);
}

TEST_F(BuildPlacedObjectTest, TransformAppliedToRoot)
{
    ObjectInstance object = MakeGridCube();
    object.positionX = 3.0f;
    object.positionY = 4.0f;
    object.positionZ = 5.0f;

    auto obj = Build(object);
    ASSERT_NE(obj, nullptr);
    const Vector3 pos = obj->Root().Position();
    EXPECT_FLOAT_EQ(pos.x, 3.0f);
    EXPECT_FLOAT_EQ(pos.y, 4.0f);
    EXPECT_FLOAT_EQ(pos.z, 5.0f);
}

TEST_F(BuildPlacedObjectTest, GridCubeWorldAabbMatchesCellHalfExtents)
{
    auto obj = Build(MakeGridCube());
    ASSERT_NE(obj, nullptr);
    auto* box = FindComponent<NS::Scene::BoxColliderComponent>(*obj);
    ASSERT_NE(box, nullptr);
    const NS::Math::AABB aabb = box->WorldAABB();
    EXPECT_NEAR(aabb.Center.x, 0.0f, 1e-4f);
    EXPECT_NEAR(aabb.Center.y, 0.0f, 1e-4f);
    EXPECT_NEAR(aabb.Center.z, 0.0f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.x, 0.5f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.y, 0.5f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.z, 0.5f, 1e-4f);
}

TEST_F(BuildPlacedObjectTest, FreeBoxWorldAabbReflectsPositionAndHalfExtents)
{
    ObjectInstance object = MakeFreeCube(ShapeCollider::Box, Vector3{1.0f, 2.0f, 3.0f});
    object.positionX = 2.0f;

    auto obj = Build(object);
    ASSERT_NE(obj, nullptr);
    auto* box = FindComponent<NS::Scene::BoxColliderComponent>(*obj);
    ASSERT_NE(box, nullptr);
    const NS::Math::AABB aabb = box->WorldAABB();
    EXPECT_NEAR(aabb.Center.x, 2.0f, 1e-4f);
    EXPECT_NEAR(aabb.Center.y, 0.0f, 1e-4f);
    EXPECT_NEAR(aabb.Center.z, 0.0f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.x, 1.0f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.y, 2.0f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.z, 3.0f, 1e-4f);
}

// components 一覧を持つ object は registry でコンポを生成し、 反射 set でフィールドが入る
TEST_F(BuildPlacedObjectTest, ComponentsDriveBuild)
{
    ObjectInstance object;
    object.flags = 0;

    NS::Game::Level::ComponentData box;
    box.typeName = "BoxColliderComponent";
    box.fields.push_back(NS::Game::Level::FieldValue{"Half Extents", Vector3{1.0f, 2.0f, 3.0f}});
    object.components.push_back(std::move(box));

    auto obj = Build(object);
    ASSERT_NE(obj, nullptr);
    auto* boxComp = FindComponent<NS::Scene::BoxColliderComponent>(*obj);
    ASSERT_NE(boxComp, nullptr);
    const Vector3 half = boxComp->HalfExtents();
    EXPECT_FLOAT_EQ(half.x, 1.0f);
    EXPECT_FLOAT_EQ(half.y, 2.0f);
    EXPECT_FLOAT_EQ(half.z, 3.0f);
}

// components を持たない object は配置物として組まれず nullptr が返る
TEST_F(BuildPlacedObjectTest, EmptyComponentsBuildsNothing)
{
    ObjectInstance object;
    object.flags = NS::Game::Level::kObjectFlagGridAligned;
    ASSERT_TRUE(object.components.empty());

    EXPECT_EQ(Build(object), nullptr);
}

// material asset path に .. を含む値は ContentRoot 外解決を拒否し、 共有 fallback へ倒れてクラッシュしない
TEST_F(BuildPlacedObjectTest, AssetPathTraversalRejectedFallsBackToDefault)
{
    ObjectInstance object = MakeFreeCube(ShapeCollider::Box);
    object.materialIndex = 0;
    std::vector<std::string> traversalPaths = {"../evil.mat"};

    auto obj = BuildPlacedObject(object, m_assets, traversalPaths);
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(Has<NS::Scene::MeshRendererComponent>(*obj));
    EXPECT_TRUE(Has<NS::Scene::BoxColliderComponent>(*obj));
}
