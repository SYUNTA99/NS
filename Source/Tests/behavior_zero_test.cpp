#include <gtest/gtest.h>

#include <Framework/Math/Math.h>
#include <Framework/Physics/Capsule.h>
#include <Framework/Physics/Sphere.h>
#include <Framework/Physics/SweptOBB.h>
#include <Framework/Physics/SweptTriangle.h>
#include <Framework/Scene/AssetManager.h>
#include <Framework/Scene/Components/BoxColliderComponent.h>
#include <Framework/Scene/Components/CapsuleColliderComponent.h>
#include <Framework/Scene/Components/HazardComponent.h>
#include <Framework/Scene/Components/SlopeColliderComponent.h>
#include <Framework/Scene/Components/SphereColliderComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Game/Blocks/BuildPlacedObject.h>
#include <Game/Level/LevelObjects.h>
#include <Game/Level/LevelJson.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using NS::Game::Blocks::BuildPlacedObject;
    using NS::Game::Blocks::FindComponent;
    using NS::Scene::ComponentData;
    using NS::Game::Level::DeserializeLevelFromJson;
    using NS::Scene::FieldValue;
    using NS::Scene::SceneData;
    using NS::Scene::ObjectData;
    using NS::Game::Level::SerializeLevelToJson;
    using NS::Math::Vector3;

    constexpr float kTol = 1e-4f;

    void ExpectVec3Near(const Vector3& expected, const Vector3& actual)
    {
        EXPECT_NEAR(expected.x, actual.x, kTol);
        EXPECT_NEAR(expected.y, actual.y, kTol);
        EXPECT_NEAR(expected.z, actual.z, kTol);
    }

    // collider channel の同一性を測る指紋。 型の有無と当たり幾何を持ち、 mesh / material に依存しない
    struct ColliderSignature
    {
        bool hasBox = false;
        bool hasSphere = false;
        bool hasCapsule = false;
        bool hasSlope = false;
        bool hasHazard = false;
        NS::Math::AABB boxAabb{};
        NS::Physics::OBB boxObb;
        NS::Physics::Sphere sphere;
        NS::Physics::Capsule capsule;
        float slopeAngle = 0.0f;
        std::array<NS::Physics::Triangle, 8> slopeTriangles{};
    };

    ColliderSignature ExtractColliderSignature(NS::Scene::GameObject& obj)
    {
        ColliderSignature sig;
        if (auto* box = FindComponent<NS::Scene::BoxColliderComponent>(obj))
        {
            sig.hasBox = true;
            sig.boxAabb = box->WorldAABB();
            sig.boxObb = box->WorldOBB();
        }
        if (auto* sphere = FindComponent<NS::Scene::SphereColliderComponent>(obj))
        {
            sig.hasSphere = true;
            sig.sphere = sphere->WorldSphere();
        }
        if (auto* capsule = FindComponent<NS::Scene::CapsuleColliderComponent>(obj))
        {
            sig.hasCapsule = true;
            sig.capsule = capsule->WorldCapsule();
        }
        if (auto* slope = FindComponent<NS::Scene::SlopeColliderComponent>(obj))
        {
            sig.hasSlope = true;
            sig.slopeAngle = slope->AngleDegrees();
            sig.slopeTriangles = slope->WorldTriangles();
        }
        if (FindComponent<NS::Scene::HazardComponent>(obj))
            sig.hasHazard = true;
        return sig;
    }

    void ExpectSignatureEqual(const ColliderSignature& expected, const ColliderSignature& actual)
    {
        EXPECT_EQ(expected.hasBox, actual.hasBox);
        EXPECT_EQ(expected.hasSphere, actual.hasSphere);
        EXPECT_EQ(expected.hasCapsule, actual.hasCapsule);
        EXPECT_EQ(expected.hasSlope, actual.hasSlope);
        EXPECT_EQ(expected.hasHazard, actual.hasHazard);

        if (expected.hasBox && actual.hasBox)
        {
            ExpectVec3Near(expected.boxAabb.Center, actual.boxAabb.Center);
            ExpectVec3Near(expected.boxAabb.Extents, actual.boxAabb.Extents);
            ExpectVec3Near(expected.boxObb.center, actual.boxObb.center);
            ExpectVec3Near(expected.boxObb.axisX, actual.boxObb.axisX);
            ExpectVec3Near(expected.boxObb.axisY, actual.boxObb.axisY);
            ExpectVec3Near(expected.boxObb.axisZ, actual.boxObb.axisZ);
            ExpectVec3Near(expected.boxObb.halfExtents, actual.boxObb.halfExtents);
        }
        if (expected.hasSphere && actual.hasSphere)
        {
            ExpectVec3Near(expected.sphere.center, actual.sphere.center);
            EXPECT_NEAR(expected.sphere.radius, actual.sphere.radius, kTol);
        }
        if (expected.hasCapsule && actual.hasCapsule)
        {
            ExpectVec3Near(expected.capsule.center, actual.capsule.center);
            ExpectVec3Near(expected.capsule.axis, actual.capsule.axis);
            EXPECT_NEAR(expected.capsule.radius, actual.capsule.radius, kTol);
            EXPECT_NEAR(expected.capsule.halfHeight, actual.capsule.halfHeight, kTol);
        }
        if (expected.hasSlope && actual.hasSlope)
        {
            EXPECT_NEAR(expected.slopeAngle, actual.slopeAngle, kTol);
            for (std::size_t i = 0; i < expected.slopeTriangles.size(); ++i)
            {
                ExpectVec3Near(expected.slopeTriangles[i].v0, actual.slopeTriangles[i].v0);
                ExpectVec3Near(expected.slopeTriangles[i].v1, actual.slopeTriangles[i].v1);
                ExpectVec3Near(expected.slopeTriangles[i].v2, actual.slopeTriangles[i].v2);
            }
        }
    }

    ComponentData MakeComponent(std::string typeName, std::vector<FieldValue> fields)
    {
        ComponentData component;
        component.typeName = std::move(typeName);
        component.fields = std::move(fields);
        return component;
    }
} // namespace

// components 駆動 object を JSON 往復しても新経路で同一 channel を組み、 反射 set が値を復元する
TEST(BehaviorZero, ComponentsDrivenSurvivesJsonRoundTrip)
{
    SceneData src;
    ObjectData obj;
    obj.positionX = 2.0f;
    obj.positionY = 1.0f;
    obj.positionZ = 3.0f;
    obj.components.push_back(MakeComponent("MeshRendererComponent", {}));
    obj.components.push_back(MakeComponent("BoxColliderComponent",
                                           {FieldValue{"Half Extents", Vector3{1.0f, 2.0f, 3.0f}},
                                            FieldValue{"Center Offset", Vector3{0.1f, 0.2f, 0.3f}}}));
    obj.components.push_back(
        MakeComponent("SphereColliderComponent",
                      {FieldValue{"Radius", 0.7f}, FieldValue{"Center Offset", Vector3{0.0f, 1.0f, 0.0f}}}));
    obj.components.push_back(
        MakeComponent("CapsuleColliderComponent", {FieldValue{"Radius", 0.4f}, FieldValue{"Half Height", 0.9f}}));
    src.objects.push_back(std::move(obj));
    src.objects.push_back(NS::Game::Level::MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));

    SceneData restored;
    ASSERT_TRUE(DeserializeLevelFromJson(restored, SerializeLevelToJson(src)));
    // 末尾に追従カメラが 1 台合成される
    ASSERT_EQ(restored.objects.size(), 3u);
    ASSERT_FALSE(restored.objects[0].components.empty()); // 往復後も新経路の components 駆動を通る

    NS::Scene::AssetManager assets{std::filesystem::path{"."}};
    const std::vector<std::string> noPaths;
    auto before = BuildPlacedObject(src.objects[0], assets, noPaths);
    auto after = BuildPlacedObject(restored.objects[0], assets, noPaths);
    ASSERT_NE(before, nullptr);
    ASSERT_NE(after, nullptr);

    const ColliderSignature beforeSig = ExtractColliderSignature(*before);
    EXPECT_TRUE(beforeSig.hasBox && beforeSig.hasSphere && beforeSig.hasCapsule);
    ExpectSignatureEqual(beforeSig, ExtractColliderSignature(*after));

    // BuildFromComponents の反射 set が実際に効いたことを before 側の box 寸法で直接確認する
    auto* box = FindComponent<NS::Scene::BoxColliderComponent>(*before);
    ASSERT_NE(box, nullptr);
    const Vector3 half = box->HalfExtents();
    EXPECT_NEAR(half.x, 1.0f, kTol);
    EXPECT_NEAR(half.y, 2.0f, kTol);
    EXPECT_NEAR(half.z, 3.0f, kTol);
}
