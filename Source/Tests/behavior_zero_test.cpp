#include "Game/Player.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <Game/Level/HazardComponent.h>
#include <gtest/gtest.h>
#include <Runtime/Math/Math.h>
#include <Runtime/Object/AssetManager.h>
#include <Runtime/Object/Components/BoxColliderComponent.h>
#include <Runtime/Object/Components/CapsuleColliderComponent.h>
#include <Runtime/Object/Components/SlopeColliderComponent.h>
#include <Runtime/Object/Components/SphereColliderComponent.h>
#include <Runtime/Object/Components/TransformComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Reflection/ObjectBuilder.h>
#include <Runtime/Object/Scene/SceneJson.h>
#include <Runtime/Physics/Capsule.h>
#include <Runtime/Physics/SweptTriangle.h>
#include <utility>
#include <vector>

namespace
{
    using NS::Object::DeserializeSceneFromJson;
    using NS::Object::SceneData;
    using NS::Object::ObjectData;
    using NS::Object::SerializeSceneToJson;
    using NS::Math::Vector3;

    constexpr float k_Tol = 1e-4f;

    void ExpectVec3Near(const Vector3& expected, const Vector3& actual)
    {
        EXPECT_NEAR(expected.x, actual.x, k_Tol);
        EXPECT_NEAR(expected.y, actual.y, k_Tol);
        EXPECT_NEAR(expected.z, actual.z, k_Tol);
    }

    // collider channel の比較用。 型の有無と当たり幾何だけ持ち、 mesh / material は見ない
    struct ColliderSignature
    {
        bool hasBox = false;
        bool hasSphere = false;
        bool hasCapsule = false;
        bool hasSlope = false;
        bool hasHazard = false;
        NS::Math::AABB boxAabb{};
        NS::Math::OBB boxObb;
        NS::Math::Sphere sphere;
        NS::Physics::Capsule capsule;
        float slopeAngle = 0.0f;
        std::array<NS::Physics::Triangle, 8> slopeTriangles{};
    };

    ColliderSignature ExtractColliderSignature(NS::Object::GameObject& obj)
    {
        ColliderSignature sig;
        if (auto* box = obj.FindComponent<NS::Object::BoxColliderComponent>())
        {
            sig.hasBox = true;
            sig.boxAabb = box->WorldAABB();
            sig.boxObb = box->WorldOBB();
        }
        if (auto* sphere = obj.FindComponent<NS::Object::SphereColliderComponent>())
        {
            sig.hasSphere = true;
            sig.sphere = sphere->WorldSphere();
        }
        if (auto* capsule = obj.FindComponent<NS::Object::CapsuleColliderComponent>())
        {
            sig.hasCapsule = true;
            sig.capsule = capsule->WorldCapsule();
        }
        if (auto* slope = obj.FindComponent<NS::Object::SlopeColliderComponent>())
        {
            sig.hasSlope = true;
            sig.slopeAngle = slope->AngleDegrees();
            sig.slopeTriangles = slope->WorldTriangles();
        }
        if (obj.FindComponent<NS::Game::Level::HazardComponent>())
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
            EXPECT_NEAR(expected.boxObb.halfExtentX, actual.boxObb.halfExtentX, k_Tol);
            EXPECT_NEAR(expected.boxObb.halfExtentY, actual.boxObb.halfExtentY, k_Tol);
            EXPECT_NEAR(expected.boxObb.halfExtentZ, actual.boxObb.halfExtentZ, k_Tol);
        }
        if (expected.hasSphere && actual.hasSphere)
        {
            ExpectVec3Near(expected.sphere.center, actual.sphere.center);
            EXPECT_NEAR(expected.sphere.radius, actual.sphere.radius, k_Tol);
        }
        if (expected.hasCapsule && actual.hasCapsule)
        {
            ExpectVec3Near(expected.capsule.center, actual.capsule.center);
            ExpectVec3Near(expected.capsule.axis, actual.capsule.axis);
            EXPECT_NEAR(expected.capsule.radius, actual.capsule.radius, k_Tol);
            EXPECT_NEAR(expected.capsule.halfHeight, actual.capsule.halfHeight, k_Tol);
        }
        if (expected.hasSlope && actual.hasSlope)
        {
            EXPECT_NEAR(expected.slopeAngle, actual.slopeAngle, k_Tol);
            for (std::size_t i = 0; i < expected.slopeTriangles.size(); ++i)
            {
                ExpectVec3Near(expected.slopeTriangles[i].v0, actual.slopeTriangles[i].v0);
                ExpectVec3Near(expected.slopeTriangles[i].v1, actual.slopeTriangles[i].v1);
                ExpectVec3Near(expected.slopeTriangles[i].v2, actual.slopeTriangles[i].v2);
            }
        }
    }

} // namespace

// components 駆動の object が JSON 往復後も同じ collider channel に組み上がるか確かめる
TEST(BehaviorZero, ComponentsDrivenSurvivesJsonRoundTrip)
{
    SceneData src;
    ObjectData obj;
    NS::Object::SetObjectPosition(obj, Vector3{2.0f, 1.0f, 3.0f});
    obj.components.push_back(NS::Object::MakeComponentEntry("MeshRendererComponent"));
    nlohmann::json boxEntry = NS::Object::MakeComponentEntry("BoxColliderComponent");
    NS::Object::SetField(boxEntry, "半径", Vector3{1.0f, 2.0f, 3.0f});
    NS::Object::SetField(boxEntry, "中心オフセット", Vector3{0.1f, 0.2f, 0.3f});
    obj.components.push_back(std::move(boxEntry));
    nlohmann::json sphere = NS::Object::MakeComponentEntry("SphereColliderComponent");
    NS::Object::SetField(sphere, "半径", 0.7f);
    NS::Object::SetField(sphere, "中心オフセット", Vector3{0.0f, 1.0f, 0.0f});
    obj.components.push_back(std::move(sphere));
    nlohmann::json capsule = NS::Object::MakeComponentEntry("CapsuleColliderComponent");
    NS::Object::SetField(capsule, "半径", 0.4f);
    NS::Object::SetField(capsule, "半分の高さ", 0.9f);
    obj.components.push_back(std::move(capsule));
    src.objects.push_back(std::move(obj));
    src.objects.push_back(MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));

    SceneData restored;
    ASSERT_TRUE(DeserializeSceneFromJson(restored, SerializeSceneToJson(src)));
    ASSERT_EQ(restored.objects.size(), 2u);
    ASSERT_FALSE(restored.objects[0].components.empty()); // 往復後も components 駆動で組ませる前提

    NS::Object::AssetManager assets{std::filesystem::path{"."}};
    auto before = NS::Object::BuildSceneObject(src.objects[0], &assets);
    auto after = NS::Object::BuildSceneObject(restored.objects[0], &assets);
    ASSERT_NE(before, nullptr);
    ASSERT_NE(after, nullptr);

    const ColliderSignature beforeSig = ExtractColliderSignature(*before);
    EXPECT_TRUE(beforeSig.hasBox && beforeSig.hasSphere && beforeSig.hasCapsule);
    ExpectSignatureEqual(beforeSig, ExtractColliderSignature(*after));

    // リフレクション set が効いたか box の寸法で直接確かめる
    auto* box = before->FindComponent<NS::Object::BoxColliderComponent>();
    ASSERT_NE(box, nullptr);
    const Vector3 half = box->HalfExtents();
    EXPECT_NEAR(half.x, 1.0f, k_Tol);
    EXPECT_NEAR(half.y, 2.0f, k_Tol);
    EXPECT_NEAR(half.z, 3.0f, k_Tol);
}
