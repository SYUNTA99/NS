#include <gtest/gtest.h>

#include <Framework/Scene/AssetManager.h>
#include <Framework/Scene/Components/BoxColliderComponent.h>
#include <Framework/Scene/Components/CapsuleColliderComponent.h>
#include <Framework/Scene/Components/HazardComponent.h>
#include <Framework/Scene/Components/MeshRendererComponent.h>
#include <Framework/Scene/Components/PoleComponent.h>
#include <Framework/Scene/Components/SlopeColliderComponent.h>
#include <Framework/Scene/Components/SphereColliderComponent.h>
#include <Game/Blocks/BlockRegistry.h>
#include <Game/Blocks/BuildPlacedObject.h>
#include <Game/Level/LevelData.h>

#include <filesystem>
#include <vector>

namespace
{
    using NS::Game::Blocks::BuildPlacedObject;
    using NS::Game::Blocks::FindComponent;
    using NS::Game::Blocks::kBlockIdDecoration;
    using NS::Game::Blocks::kBlockIdHazard;
    using NS::Game::Blocks::kBlockIdPole;
    using NS::Game::Blocks::kBlockIdSlope15;
    using NS::Game::Blocks::kBlockIdSlope22;
    using NS::Game::Blocks::kBlockIdSlope30;
    using NS::Game::Blocks::kBlockIdSlope45;
    using NS::Game::Blocks::kBlockIdSolid;
    using NS::Game::Blocks::kBlockIdWater;
    using NS::Game::Level::kObjectFlagGridAligned;
    using NS::Game::Level::ObjectInstance;
    using NS::Game::Level::SetObjectShapeCollider;
    using NS::Game::Level::ShapeCollider;
    using NS::Math::Vector3;

    // device を確立しない AssetManager。 Builtin / SharedMaterial は nullptr を返すが、 ファクトリは落ちない
    // 構築時は path を保持するだけ、 builtin 未登録なので GPU を一切触らない
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

    ObjectInstance MakeGrid(std::uint16_t kind)
    {
        ObjectInstance object;
        object.kind = kind;
        object.flags = kObjectFlagGridAligned;
        return object;
    }

    ObjectInstance MakeFree(ShapeCollider shape)
    {
        ObjectInstance object;
        object.kind = kBlockIdSolid;
        object.flags = 0;
        SetObjectShapeCollider(object, shape);
        return object;
    }

    template <class T> bool Has(NS::Scene::GameObject& obj)
    {
        return FindComponent<T>(obj) != nullptr;
    }
} // namespace

TEST_F(BuildPlacedObjectTest, SolidGridHasOnlyBoxCollider)
{
    auto obj = Build(MakeGrid(kBlockIdSolid));
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(Has<NS::Scene::MeshRendererComponent>(*obj));
    EXPECT_TRUE(Has<NS::Scene::BoxColliderComponent>(*obj));
    EXPECT_FALSE(Has<NS::Scene::SlopeColliderComponent>(*obj));
    EXPECT_FALSE(Has<NS::Scene::PoleComponent>(*obj));
    EXPECT_FALSE(Has<NS::Scene::HazardComponent>(*obj));
    EXPECT_FALSE(Has<NS::Scene::SphereColliderComponent>(*obj));
    EXPECT_FALSE(Has<NS::Scene::CapsuleColliderComponent>(*obj));
}

TEST_F(BuildPlacedObjectTest, Slope45HasSlopeColliderAt45)
{
    auto obj = Build(MakeGrid(kBlockIdSlope45));
    ASSERT_NE(obj, nullptr);
    auto* slope = FindComponent<NS::Scene::SlopeColliderComponent>(*obj);
    ASSERT_NE(slope, nullptr);
    EXPECT_FLOAT_EQ(slope->AngleDegrees(), 45.0f);
    EXPECT_FALSE(Has<NS::Scene::BoxColliderComponent>(*obj));
}

TEST_F(BuildPlacedObjectTest, Slope30Angle)
{
    auto obj = Build(MakeGrid(kBlockIdSlope30));
    ASSERT_NE(obj, nullptr);
    auto* slope = FindComponent<NS::Scene::SlopeColliderComponent>(*obj);
    ASSERT_NE(slope, nullptr);
    EXPECT_FLOAT_EQ(slope->AngleDegrees(), 30.0f);
}

TEST_F(BuildPlacedObjectTest, Slope22Angle)
{
    auto obj = Build(MakeGrid(kBlockIdSlope22));
    ASSERT_NE(obj, nullptr);
    auto* slope = FindComponent<NS::Scene::SlopeColliderComponent>(*obj);
    ASSERT_NE(slope, nullptr);
    EXPECT_FLOAT_EQ(slope->AngleDegrees(), 22.5f);
}

TEST_F(BuildPlacedObjectTest, Slope15Angle)
{
    auto obj = Build(MakeGrid(kBlockIdSlope15));
    ASSERT_NE(obj, nullptr);
    auto* slope = FindComponent<NS::Scene::SlopeColliderComponent>(*obj);
    ASSERT_NE(slope, nullptr);
    EXPECT_FLOAT_EQ(slope->AngleDegrees(), 15.0f);
}

TEST_F(BuildPlacedObjectTest, PoleHasPoleComponentWithCustomDimensions)
{
    auto obj = Build(MakeGrid(kBlockIdPole));
    ASSERT_NE(obj, nullptr);
    auto* pole = FindComponent<NS::Scene::PoleComponent>(*obj);
    ASSERT_NE(pole, nullptr);
    EXPECT_FLOAT_EQ(pole->Radius(), 0.15f);
    EXPECT_FLOAT_EQ(pole->Height(), 1.0f);
    EXPECT_FALSE(Has<NS::Scene::BoxColliderComponent>(*obj));
    EXPECT_FALSE(Has<NS::Scene::SlopeColliderComponent>(*obj));
}

TEST_F(BuildPlacedObjectTest, HazardHasBoxAndHazardComponent)
{
    auto obj = Build(MakeGrid(kBlockIdHazard));
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(Has<NS::Scene::BoxColliderComponent>(*obj));
    EXPECT_TRUE(Has<NS::Scene::HazardComponent>(*obj));
}

TEST_F(BuildPlacedObjectTest, WaterHasNoCollider)
{
    auto obj = Build(MakeGrid(kBlockIdWater));
    ASSERT_NE(obj, nullptr);
    EXPECT_FALSE(Has<NS::Scene::BoxColliderComponent>(*obj));
    EXPECT_FALSE(Has<NS::Scene::SlopeColliderComponent>(*obj));
    EXPECT_FALSE(Has<NS::Scene::SphereColliderComponent>(*obj));
    EXPECT_FALSE(Has<NS::Scene::CapsuleColliderComponent>(*obj));
}

TEST_F(BuildPlacedObjectTest, DecorationHasNoCollider)
{
    auto obj = Build(MakeGrid(kBlockIdDecoration));
    ASSERT_NE(obj, nullptr);
    EXPECT_FALSE(Has<NS::Scene::BoxColliderComponent>(*obj));
    EXPECT_FALSE(Has<NS::Scene::SlopeColliderComponent>(*obj));
    EXPECT_FALSE(Has<NS::Scene::SphereColliderComponent>(*obj));
    EXPECT_FALSE(Has<NS::Scene::CapsuleColliderComponent>(*obj));
}

TEST_F(BuildPlacedObjectTest, FreeBoxHasBoxColliderWithSavedHalfExtents)
{
    ObjectInstance object = MakeFree(ShapeCollider::Box);
    object.colliderHalfExtentsX = 1.0f;
    object.colliderHalfExtentsY = 2.0f;
    object.colliderHalfExtentsZ = 3.0f;

    auto obj = Build(object);
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

TEST_F(BuildPlacedObjectTest, FreeSphereHasInternalBoxPlusSphere)
{
    ObjectInstance object = MakeFree(ShapeCollider::Sphere);
    object.colliderHalfExtentsX = 0.7f; // 球半径
    object.colliderOffsetY = 1.0f;

    auto obj = Build(object);
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(Has<NS::Scene::BoxColliderComponent>(*obj)); // 内蔵 Box は残る
    auto* sphere = FindComponent<NS::Scene::SphereColliderComponent>(*obj);
    ASSERT_NE(sphere, nullptr);
    EXPECT_FALSE(Has<NS::Scene::CapsuleColliderComponent>(*obj));

    const NS::Physics::Sphere world = sphere->WorldSphere();
    EXPECT_NEAR(world.radius, 0.7f, 1e-4f);
    EXPECT_NEAR(world.center.y, 1.0f, 1e-4f); // offset 反映
}

TEST_F(BuildPlacedObjectTest, FreeCapsuleHasInternalBoxPlusCapsule)
{
    ObjectInstance object = MakeFree(ShapeCollider::Capsule);
    object.colliderHalfExtentsX = 0.4f; // 半径
    object.colliderHalfExtentsY = 0.9f; // 半高

    auto obj = Build(object);
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(Has<NS::Scene::BoxColliderComponent>(*obj)); // 内蔵 Box は残る
    auto* capsule = FindComponent<NS::Scene::CapsuleColliderComponent>(*obj);
    ASSERT_NE(capsule, nullptr);
    EXPECT_FALSE(Has<NS::Scene::SphereColliderComponent>(*obj));

    const NS::Physics::Capsule world = capsule->WorldCapsule();
    EXPECT_NEAR(world.radius, 0.4f, 1e-4f);
    EXPECT_NEAR(world.halfHeight, 0.9f, 1e-4f);
}

TEST_F(BuildPlacedObjectTest, TransformAppliedToRoot)
{
    ObjectInstance object = MakeGrid(kBlockIdSolid);
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

TEST_F(BuildPlacedObjectTest, SolidGridWorldAabbMatchesCellHalfExtents)
{
    auto obj = Build(MakeGrid(kBlockIdSolid));
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
    ObjectInstance object = MakeFree(ShapeCollider::Box);
    object.positionX = 2.0f;
    object.colliderHalfExtentsX = 1.0f;
    object.colliderHalfExtentsY = 2.0f;
    object.colliderHalfExtentsZ = 3.0f;

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
