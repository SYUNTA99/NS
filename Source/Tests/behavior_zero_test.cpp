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
#include <Framework/Scene/Components/MeshRendererComponent.h>
#include <Framework/Scene/Components/PickupComponent.h>
#include <Framework/Scene/Components/PoleComponent.h>
#include <Framework/Scene/Components/SlopeColliderComponent.h>
#include <Framework/Scene/Components/SphereColliderComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Game/Blocks/BlockRegistry.h>
#include <Game/Blocks/BuildPlacedObject.h>
#include <Game/Level/LevelData.h>
#include <Game/Level/LevelJson.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    using NS::Game::Blocks::BuildPlacedObject;
    using NS::Game::Blocks::FindComponent;
    using NS::Game::Blocks::kBlockIdCoin;
    using NS::Game::Blocks::kBlockIdDecoration;
    using NS::Game::Blocks::kBlockIdHazard;
    using NS::Game::Blocks::kBlockIdPole;
    using NS::Game::Blocks::kBlockIdPowerStar;
    using NS::Game::Blocks::kBlockIdSlope45;
    using NS::Game::Blocks::kBlockIdSolid;
    using NS::Game::Blocks::kBlockIdWater;
    using NS::Game::Blocks::MaterializeLegacyKind;
    using NS::Game::Blocks::MigrateLegacyLevel;
    using NS::Game::Level::CameraVolume;
    using NS::Game::Level::ComponentData;
    using NS::Game::Level::DeserializeLevelFromJson;
    using NS::Game::Level::FieldValue;
    using NS::Game::Level::kObjectFlagGridAligned;
    using NS::Game::Level::LevelData;
    using NS::Game::Level::ObjectInstance;
    using NS::Game::Level::SerializeLevelToJson;
    using NS::Game::Level::SetObjectShapeCollider;
    using NS::Game::Level::ShapeCollider;
    using NS::Math::Vector3;

    constexpr float kTol = 1e-4f;

    void ExpectVec3Near(const Vector3& expected, const Vector3& actual)
    {
        EXPECT_NEAR(expected.x, actual.x, kTol);
        EXPECT_NEAR(expected.y, actual.y, kTol);
        EXPECT_NEAR(expected.z, actual.z, kTol);
    }

    // collider channel の同一性を測る指紋。 型の有無と AABB / OBB / 球 / カプセル / 三角形の当たり幾何を持ち
    // mesh や material には依存しないので device を持たない環境でも成立する
    struct ColliderSignature
    {
        bool hasBox = false;
        bool hasSphere = false;
        bool hasCapsule = false;
        bool hasSlope = false;
        bool hasPole = false;
        bool hasHazard = false;
        NS::Math::AABB boxAabb{};
        NS::Physics::OBB boxObb;
        NS::Physics::Sphere sphere;
        NS::Physics::Capsule capsule;
        float slopeAngle = 0.0f;
        std::array<NS::Physics::Triangle, 8> slopeTriangles{};
        float poleRadius = 0.0f;
        float poleHeight = 0.0f;
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
        if (auto* pole = FindComponent<NS::Scene::PoleComponent>(obj))
        {
            sig.hasPole = true;
            sig.poleRadius = pole->Radius();
            sig.poleHeight = pole->Height();
        }
        if (FindComponent<NS::Scene::HazardComponent>(obj))
            sig.hasHazard = true;
        return sig;
    }

    bool HasAnyColliderChannel(const ColliderSignature& sig)
    {
        return sig.hasBox || sig.hasSphere || sig.hasCapsule || sig.hasSlope || sig.hasPole;
    }

    void ExpectSignatureEqual(const ColliderSignature& expected, const ColliderSignature& actual)
    {
        EXPECT_EQ(expected.hasBox, actual.hasBox);
        EXPECT_EQ(expected.hasSphere, actual.hasSphere);
        EXPECT_EQ(expected.hasCapsule, actual.hasCapsule);
        EXPECT_EQ(expected.hasSlope, actual.hasSlope);
        EXPECT_EQ(expected.hasPole, actual.hasPole);
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
        if (expected.hasPole && actual.hasPole)
        {
            EXPECT_NEAR(expected.poleRadius, actual.poleRadius, kTol);
            EXPECT_NEAR(expected.poleHeight, actual.poleHeight, kTol);
        }
    }

    ObjectInstance MakeGrid(std::uint16_t kind, float x, float y, float z)
    {
        ObjectInstance object;
        object.kind = kind;
        object.flags = kObjectFlagGridAligned;
        object.positionX = x;
        object.positionY = y;
        object.positionZ = z;
        return object;
    }

    ObjectInstance MakeFree(ShapeCollider shape, float x, float y, float z)
    {
        ObjectInstance object;
        object.kind = kBlockIdSolid;
        object.flags = 0;
        object.positionX = x;
        object.positionY = y;
        object.positionZ = z;
        SetObjectShapeCollider(object, shape);
        return object;
    }

    ComponentData MakeComponent(std::string typeName, std::vector<FieldValue> fields)
    {
        ComponentData component;
        component.typeName = std::move(typeName);
        component.fields = std::move(fields);
        return component;
    }

    // object.kind を materializer の kind 引数へ渡す薄いアダプタ。 旧 1 引数呼出の検証をそのまま保つ
    std::vector<ComponentData> MaterializeKind(const ObjectInstance& object)
    {
        return MaterializeLegacyKind(object.kind, object);
    }

    // ComponentData の反射フィールドを名前で引く data 段ヘルパ群。 device を持たずに materialize 結果を直接検証する
    const FieldValue* FindFieldValue(const ComponentData& component, const std::string& name)
    {
        for (const auto& field : component.fields)
            if (field.name == name)
                return &field;
        return nullptr;
    }

    std::string FieldString(const ComponentData& component, const std::string& name)
    {
        const FieldValue* field = FindFieldValue(component, name);
        if (field != nullptr && std::holds_alternative<std::string>(field->value))
            return std::get<std::string>(field->value);
        return std::string{};
    }

    float FieldFloat(const ComponentData& component, const std::string& name)
    {
        const FieldValue* field = FindFieldValue(component, name);
        if (field != nullptr && std::holds_alternative<float>(field->value))
            return std::get<float>(field->value);
        return 0.0f;
    }

    int FieldInt(const ComponentData& component, const std::string& name)
    {
        const FieldValue* field = FindFieldValue(component, name);
        if (field != nullptr && std::holds_alternative<int>(field->value))
            return std::get<int>(field->value);
        return -1;
    }

    bool HasComponentType(const std::vector<ComponentData>& components, const std::string& typeName)
    {
        for (const auto& component : components)
            if (component.typeName == typeName)
                return true;
        return false;
    }

    // grid と free と全 collider channel を含む代表レベル。 旧形式の components 空 kind 駆動で組む
    LevelData MakeRepresentativeLevel()
    {
        LevelData level;

        level.objects.push_back(MakeGrid(kBlockIdSolid, 1.0f, 0.0f, 0.0f));
        level.objects.push_back(MakeGrid(kBlockIdSlope45, 2.0f, 0.0f, 0.0f));
        level.objects.push_back(MakeGrid(kBlockIdPole, 3.0f, 0.0f, 0.0f));
        level.objects.push_back(MakeGrid(kBlockIdHazard, 4.0f, 0.0f, 0.0f));
        // 当たりを持たない代表 (water / deco) と視覚を持たない代表 (coin / star) も往復経路に通す
        level.objects.push_back(MakeGrid(kBlockIdWater, 8.0f, 0.0f, 0.0f));
        level.objects.push_back(MakeGrid(kBlockIdDecoration, 9.0f, 0.0f, 0.0f));
        level.objects.push_back(MakeGrid(kBlockIdCoin, 10.0f, 0.0f, 0.0f));
        level.objects.push_back(MakeGrid(kBlockIdPowerStar, 11.0f, 0.0f, 0.0f));

        ObjectInstance freeBox = MakeFree(ShapeCollider::Box, 5.0f, 1.5f, -2.0f);
        freeBox.materialIndex = 0;
        freeBox.colliderHalfExtentsX = 1.0f;
        freeBox.colliderHalfExtentsY = 2.0f;
        freeBox.colliderHalfExtentsZ = 3.0f;
        level.objects.push_back(freeBox);

        ObjectInstance freeSphere = MakeFree(ShapeCollider::Sphere, -3.0f, 0.5f, 4.0f);
        freeSphere.colliderHalfExtentsX = 0.7f; // 球の半径
        freeSphere.colliderOffsetY = 1.0f;
        level.objects.push_back(freeSphere);

        ObjectInstance freeCapsule = MakeFree(ShapeCollider::Capsule, 6.0f, 0.0f, 1.0f);
        freeCapsule.colliderHalfExtentsX = 0.4f; // カプセルの半径
        freeCapsule.colliderHalfExtentsY = 0.9f; // カプセルの半高
        level.objects.push_back(freeCapsule);

        // 回転と非一様 scale と非単位 colliderRotation を持つ free box で WorldOBB 経路を張る
        ObjectInstance rotatedBox = MakeFree(ShapeCollider::Box, 7.0f, 2.0f, -1.0f);
        rotatedBox.rotationY = 0.3826834f; // Y 軸 45 度の sin
        rotatedBox.rotationW = 0.9238795f; // Y 軸 45 度の cos
        rotatedBox.scaleX = 1.0f;
        rotatedBox.scaleY = 2.0f;
        rotatedBox.scaleZ = 1.5f;
        rotatedBox.colliderHalfExtentsX = 0.6f;
        rotatedBox.colliderHalfExtentsY = 0.7f;
        rotatedBox.colliderHalfExtentsZ = 0.8f;
        rotatedBox.colliderRotationY = 0.3826834f;
        rotatedBox.colliderRotationW = 0.9238795f;
        level.objects.push_back(rotatedBox);

        level.materialPaths.push_back("materials/stone.mat");

        level.spawnX = 1;
        level.spawnY = 2;
        level.spawnZ = 3;
        level.themeId = 2;
        level.bgmId = 3;
        level.coinThreshold = 10;
        level.timeLimitSeconds = 120;

        CameraVolume volume;
        volume.cameraPositionX = 5.0f;
        volume.priority = 20;
        volume.lookAtPlayer = 1;
        level.cameraVolumes.push_back(volume);

        return level;
    }
} // namespace

// 旧 kind 駆動レベルを JSON 往復した後も、 各配置物が同じ collider channel 集合と当たり幾何を組む
TEST(BehaviorZero, JsonRoundTripPreservesColliderChannels)
{
    const LevelData src = MakeRepresentativeLevel();

    LevelData restored;
    ASSERT_TRUE(DeserializeLevelFromJson(restored, SerializeLevelToJson(src)));
    ASSERT_EQ(restored.objects.size(), src.objects.size());

    NS::Scene::AssetManager assets{std::filesystem::path{"."}};

    bool anyColliderObserved = false;
    for (std::size_t i = 0; i < src.objects.size(); ++i)
    {
        // 旧形式は components 空なので往復後も空のまま kind 駆動を通り、 ここで分岐前提を固定する
        EXPECT_TRUE(src.objects[i].components.empty());
        EXPECT_TRUE(restored.objects[i].components.empty());

        auto before = BuildPlacedObject(src.objects[i], assets, src.materialPaths);
        auto after = BuildPlacedObject(restored.objects[i], assets, restored.materialPaths);
        ASSERT_NE(before, nullptr);
        ASSERT_NE(after, nullptr);

        const ColliderSignature beforeSig = ExtractColliderSignature(*before);
        anyColliderObserved = anyColliderObserved || HasAnyColliderChannel(beforeSig);
        ExpectSignatureEqual(beforeSig, ExtractColliderSignature(*after));
    }
    // water / deco / coin / star は当たり無しだが、 当たりを持つ代表も含むのでレベル全体では当たりが観測される
    // 両辺が一斉に当たり無しへ潰れて比較が素通る偽陽性をレベル単位で塞ぐ
    EXPECT_TRUE(anyColliderObserved);
}

// JSON 往復が CRC32 を保ち、 描画と当たりに効く全フィールドが意味的に同一になる
TEST(BehaviorZero, JsonRoundTripPreservesCrc)
{
    const LevelData src = MakeRepresentativeLevel();
    const std::uint32_t crcBefore = src.ComputeCrc32();

    LevelData restored;
    ASSERT_TRUE(DeserializeLevelFromJson(restored, SerializeLevelToJson(src)));
    EXPECT_EQ(restored.ComputeCrc32(), crcBefore);
}

// 同一 LevelData の 2 回直列化は byte-identical で、 直列化が決定論であることを示す
TEST(BehaviorZero, TwoSerializationsAreByteIdentical)
{
    const LevelData src = MakeRepresentativeLevel();
    EXPECT_EQ(SerializeLevelToJson(src), SerializeLevelToJson(src));
}

// JSON 往復で復元した LevelData を再直列化すると元の文字列に一致し、 正準性の不動点を示す
TEST(BehaviorZero, SerializeAfterRoundTripMatchesOriginal)
{
    const LevelData src = MakeRepresentativeLevel();
    const std::string original = SerializeLevelToJson(src);

    LevelData restored;
    ASSERT_TRUE(DeserializeLevelFromJson(restored, original));
    EXPECT_EQ(SerializeLevelToJson(restored), original);
}

// 新形式の components 駆動 BuildFromComponents が、 旧形式の kind 駆動と同じ box を同一 channel に組む
TEST(BehaviorZero, ComponentsDrivenMatchesKindDriven)
{
    NS::Scene::AssetManager assets{std::filesystem::path{"."}};
    const std::vector<std::string> noPaths;

    ObjectInstance kindBox = MakeFree(ShapeCollider::Box, 5.0f, 1.5f, -2.0f);
    kindBox.colliderHalfExtentsX = 1.0f;
    kindBox.colliderHalfExtentsY = 2.0f;
    kindBox.colliderHalfExtentsZ = 3.0f;

    ObjectInstance compBox;
    compBox.kind = kBlockIdSolid;
    compBox.flags = 0;
    compBox.positionX = 5.0f;
    compBox.positionY = 1.5f;
    compBox.positionZ = -2.0f;
    compBox.components.push_back(MakeComponent("MeshRendererComponent", {}));
    compBox.components.push_back(
        MakeComponent("BoxColliderComponent", {FieldValue{"Half Extents", Vector3{1.0f, 2.0f, 3.0f}}}));

    ASSERT_FALSE(compBox.components.empty()); // BuildFromComponents 分岐へ入ることを固定する
    auto kindBuilt = BuildPlacedObject(kindBox, assets, noPaths);
    auto compBuilt = BuildPlacedObject(compBox, assets, noPaths);
    ASSERT_NE(kindBuilt, nullptr);
    ASSERT_NE(compBuilt, nullptr);

    const ColliderSignature kindSig = ExtractColliderSignature(*kindBuilt);
    EXPECT_TRUE(kindSig.hasBox); // 等価比較が空 signature 同士の素通りでないことを保証する
    ExpectSignatureEqual(kindSig, ExtractColliderSignature(*compBuilt));
}

// components 駆動 object を JSON 往復しても新経路で同一 channel を組み、 反射 set が値を復元する
TEST(BehaviorZero, ComponentsDrivenSurvivesJsonRoundTrip)
{
    LevelData src;
    ObjectInstance obj;
    obj.kind = kBlockIdSolid;
    obj.flags = 0;
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

    LevelData restored;
    ASSERT_TRUE(DeserializeLevelFromJson(restored, SerializeLevelToJson(src)));
    ASSERT_EQ(restored.objects.size(), 1u);
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

// kind が materialize する ComponentData がレシピどおりで、 kind 駆動と手書き components が同一 collider を組む
TEST(BehaviorZero, MaterializedKindMatchesAuthoredComponents)
{
    // grid solid: cube + block material + BoxCollider
    {
        const auto comps = MaterializeKind(MakeGrid(kBlockIdSolid, 0.0f, 0.0f, 0.0f));
        ASSERT_EQ(comps.size(), 2u);
        EXPECT_EQ(comps[0].typeName, "MeshRendererComponent");
        EXPECT_EQ(FieldString(comps[0], "Mesh"), "cube");
        EXPECT_EQ(FieldString(comps[0], "Material"), "block");
        EXPECT_EQ(comps[1].typeName, "BoxColliderComponent");
    }
    // grid slope45: wedge45 + SlopeCollider(45 度)
    {
        const auto comps = MaterializeKind(MakeGrid(kBlockIdSlope45, 0.0f, 0.0f, 0.0f));
        ASSERT_EQ(comps.size(), 2u);
        EXPECT_EQ(FieldString(comps[0], "Mesh"), "wedge45");
        EXPECT_EQ(comps[1].typeName, "SlopeColliderComponent");
        EXPECT_FLOAT_EQ(FieldFloat(comps[1], "Angle (deg)"), 45.0f);
    }
    // grid pole: pole + PoleComponent
    {
        const auto comps = MaterializeKind(MakeGrid(kBlockIdPole, 0.0f, 0.0f, 0.0f));
        ASSERT_EQ(comps.size(), 2u);
        EXPECT_EQ(FieldString(comps[0], "Mesh"), "pole");
        EXPECT_EQ(comps[1].typeName, "PoleComponent");
    }
    // grid hazard: cube + BoxCollider + HazardComponent
    {
        const auto comps = MaterializeKind(MakeGrid(kBlockIdHazard, 0.0f, 0.0f, 0.0f));
        ASSERT_EQ(comps.size(), 3u);
        EXPECT_EQ(comps[1].typeName, "BoxColliderComponent");
        EXPECT_EQ(comps[2].typeName, "HazardComponent");
    }
    // grid water: MeshRenderer のみ・ material は water
    {
        const auto comps = MaterializeKind(MakeGrid(kBlockIdWater, 0.0f, 0.0f, 0.0f));
        ASSERT_EQ(comps.size(), 1u);
        EXPECT_EQ(comps[0].typeName, "MeshRendererComponent");
        EXPECT_EQ(FieldString(comps[0], "Material"), "water");
    }
    // grid decoration: MeshRenderer のみ・ material は block
    {
        const auto comps = MaterializeKind(MakeGrid(kBlockIdDecoration, 0.0f, 0.0f, 0.0f));
        ASSERT_EQ(comps.size(), 1u);
        EXPECT_EQ(FieldString(comps[0], "Material"), "block");
    }
    // grid coin: PickupComponent のみ (Pickup Kind 0)・ MeshRenderer を含まない (視覚ゼロ)
    {
        const auto comps = MaterializeKind(MakeGrid(kBlockIdCoin, 0.0f, 0.0f, 0.0f));
        ASSERT_EQ(comps.size(), 1u);
        EXPECT_EQ(comps[0].typeName, "PickupComponent");
        EXPECT_EQ(FieldInt(comps[0], "Pickup Kind"), 0);
        EXPECT_FALSE(HasComponentType(comps, "MeshRendererComponent"));
    }
    // grid star: PickupComponent のみ (Pickup Kind 1)・ MeshRenderer を含まない (視覚ゼロ)
    {
        const auto comps = MaterializeKind(MakeGrid(kBlockIdPowerStar, 0.0f, 0.0f, 0.0f));
        ASSERT_EQ(comps.size(), 1u);
        EXPECT_EQ(comps[0].typeName, "PickupComponent");
        EXPECT_EQ(FieldInt(comps[0], "Pickup Kind"), 1);
        EXPECT_FALSE(HasComponentType(comps, "MeshRendererComponent"));
    }
    // free sphere: cube (空 material) + BoxCollider + SphereCollider 退避
    {
        const auto comps = MaterializeKind(MakeFree(ShapeCollider::Sphere, 0.0f, 0.0f, 0.0f));
        ASSERT_EQ(comps.size(), 3u);
        EXPECT_EQ(comps[0].typeName, "MeshRendererComponent");
        EXPECT_EQ(FieldString(comps[0], "Mesh"), "cube");
        EXPECT_EQ(FieldString(comps[0], "Material"), "");
        EXPECT_EQ(comps[1].typeName, "BoxColliderComponent");
        EXPECT_EQ(comps[2].typeName, "SphereColliderComponent");
    }
    // free capsule: cube + BoxCollider + CapsuleCollider 退避
    {
        const auto comps = MaterializeKind(MakeFree(ShapeCollider::Capsule, 0.0f, 0.0f, 0.0f));
        ASSERT_EQ(comps.size(), 3u);
        EXPECT_EQ(comps[2].typeName, "CapsuleColliderComponent");
    }

    // golden 等価: kind 駆動 (materialize→build) と手書き components→build が同一 collider signature を組む
    NS::Scene::AssetManager assets{std::filesystem::path{"."}};
    const std::vector<std::string> noPaths;

    ObjectInstance kindSolid = MakeGrid(kBlockIdSolid, 1.0f, 2.0f, 3.0f);
    ObjectInstance authoredSolid;
    authoredSolid.kind = kBlockIdSolid;
    authoredSolid.flags = kObjectFlagGridAligned;
    authoredSolid.positionX = 1.0f;
    authoredSolid.positionY = 2.0f;
    authoredSolid.positionZ = 3.0f;
    authoredSolid.components.push_back(
        MakeComponent("MeshRendererComponent",
                      {FieldValue{"Mesh", std::string{"cube"}}, FieldValue{"Material", std::string{"block"}}}));
    authoredSolid.components.push_back(
        MakeComponent("BoxColliderComponent", {FieldValue{"Half Extents", Vector3{0.5f, 0.5f, 0.5f}}}));

    auto kindBuilt = BuildPlacedObject(kindSolid, assets, noPaths);
    auto authoredBuilt = BuildPlacedObject(authoredSolid, assets, noPaths);
    ASSERT_NE(kindBuilt, nullptr);
    ASSERT_NE(authoredBuilt, nullptr);

    const ColliderSignature kindSig = ExtractColliderSignature(*kindBuilt);
    EXPECT_TRUE(kindSig.hasBox);
    ExpectSignatureEqual(kindSig, ExtractColliderSignature(*authoredBuilt));
}

// コイン / スターは視覚 (MeshRenderer) も当たりも持たず PickupComponent だけを持つ — 不可視を機械検証する
TEST(BehaviorZero, CoinAndStarHaveNoVisual)
{
    NS::Scene::AssetManager assets{std::filesystem::path{"."}};
    const std::vector<std::string> noPaths;

    for (const std::uint16_t kind : {kBlockIdCoin, kBlockIdPowerStar})
    {
        auto built = BuildPlacedObject(MakeGrid(kind, 0.0f, 0.0f, 0.0f), assets, noPaths);
        ASSERT_NE(built, nullptr);
        EXPECT_EQ(FindComponent<NS::Scene::MeshRendererComponent>(*built), nullptr);
        EXPECT_NE(FindComponent<NS::Scene::PickupComponent>(*built), nullptr);
        EXPECT_FALSE(HasAnyColliderChannel(ExtractColliderSignature(*built)));
    }
}

// water / deco は当たりを持たないが、 material 参照がデータとして JSON 往復後も保たれる
TEST(BehaviorZero, WaterAndDecorationMaterialSurvivesRoundTrip)
{
    LevelData src;
    src.objects.push_back(MakeGrid(kBlockIdWater, 0.0f, 0.0f, 0.0f));
    src.objects.push_back(MakeGrid(kBlockIdDecoration, 1.0f, 0.0f, 0.0f));

    LevelData restored;
    ASSERT_TRUE(DeserializeLevelFromJson(restored, SerializeLevelToJson(src)));
    ASSERT_EQ(restored.objects.size(), 2u);

    NS::Scene::AssetManager assets{std::filesystem::path{"."}};
    const std::vector<std::string> noPaths;

    auto water = BuildPlacedObject(restored.objects[0], assets, noPaths);
    auto deco = BuildPlacedObject(restored.objects[1], assets, noPaths);
    ASSERT_NE(water, nullptr);
    ASSERT_NE(deco, nullptr);

    auto* waterMesh = FindComponent<NS::Scene::MeshRendererComponent>(*water);
    auto* decoMesh = FindComponent<NS::Scene::MeshRendererComponent>(*deco);
    ASSERT_NE(waterMesh, nullptr);
    ASSERT_NE(decoMesh, nullptr);
    EXPECT_EQ(waterMesh->MaterialRef(), "water");
    EXPECT_EQ(decoMesh->MaterialRef(), "block");
    EXPECT_FALSE(HasAnyColliderChannel(ExtractColliderSignature(*water)));
    EXPECT_FALSE(HasAnyColliderChannel(ExtractColliderSignature(*deco)));
}

namespace
{
    struct LegacyKindCase
    {
        std::uint16_t kind;
        float x;
    };

    // components 配列を持たず kind だけで配置物を表す旧形式 level JSON を組む
    std::string MakeLegacyLevelJson(const std::vector<LegacyKindCase>& cases)
    {
        std::string objects;
        for (std::size_t i = 0; i < cases.size(); ++i)
        {
            if (i != 0)
                objects += ",";
            objects += "{\"kind\":" + std::to_string(cases[i].kind) + ",\"flags\":1,\"transform\":{\"pos\":[" +
                       std::to_string(cases[i].x) + ",0,0]}}";
        }
        return "{\"objects\":[" + objects + "]}";
    }
} // namespace

// 旧形式 JSON (components 配列なし・ kind だけ) を読込→移行すると、 各 object が LegacyKind placeholder 経由で
// 実 component を持ち、 その collider channel が同じ kind を直接組んだ object と一致する
TEST(BehaviorZero, LegacyKindJsonMigratesToComponents)
{
    const std::vector<LegacyKindCase> cases = {{kBlockIdSolid, 0.0f},
                                               {kBlockIdSlope45, 1.0f},
                                               {kBlockIdPole, 2.0f},
                                               {kBlockIdHazard, 3.0f},
                                               {kBlockIdWater, 4.0f},
                                               {kBlockIdDecoration, 5.0f},
                                               {kBlockIdCoin, 6.0f},
                                               {kBlockIdPowerStar, 7.0f}};

    LevelData migrated;
    ASSERT_TRUE(DeserializeLevelFromJson(migrated, MakeLegacyLevelJson(cases)));
    ASSERT_EQ(migrated.objects.size(), cases.size());

    // 読込直後は各 object が LegacyKind placeholder を 1 つだけ持つ (まだ展開前)
    for (const auto& object : migrated.objects)
    {
        ASSERT_EQ(object.components.size(), 1u);
        EXPECT_EQ(object.components[0].typeName, "LegacyKind");
    }

    MigrateLegacyLevel(migrated);

    NS::Scene::AssetManager assets{std::filesystem::path{"."}};
    const std::vector<std::string> noPaths;

    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const ObjectInstance& obj = migrated.objects[i];

        // 移行後は placeholder が消え、 実 component を持つ
        EXPECT_FALSE(obj.components.empty());
        for (const auto& comp : obj.components)
            EXPECT_NE(comp.typeName, "LegacyKind");

        ObjectInstance direct = MakeGrid(cases[i].kind, cases[i].x, 0.0f, 0.0f);
        auto migratedBuilt = BuildPlacedObject(obj, assets, noPaths);
        auto directBuilt = BuildPlacedObject(direct, assets, noPaths);
        ASSERT_NE(migratedBuilt, nullptr);
        ASSERT_NE(directBuilt, nullptr);

        if (cases[i].kind == kBlockIdCoin || cases[i].kind == kBlockIdPowerStar)
        {
            // coin / star は PickupComponent のみ・ MeshRenderer 無し (視覚ゼロ) を保つ
            EXPECT_EQ(FindComponent<NS::Scene::MeshRendererComponent>(*migratedBuilt), nullptr);
            EXPECT_NE(FindComponent<NS::Scene::PickupComponent>(*migratedBuilt), nullptr);
        }

        ExpectSignatureEqual(ExtractColliderSignature(*directBuilt), ExtractColliderSignature(*migratedBuilt));
    }
}

// seed / 旧 BLKS バイナリ由来の components 空 + kind オブジェクトも MigrateLegacyLevel が実 component へ展開し、
// 移行前の kind 駆動 build と同一 collider channel になる。 二重 migrate しても構成は変わらない (冪等)
TEST(BehaviorZero, SeedAndBinaryKindMigratesAndIsIdempotent)
{
    LevelData level;
    // seed 相当 (MakeGridObject) と 旧 BLKS 由来 (MigrateBlocksToObjects) を混ぜる
    level.objects.push_back(NS::Game::Level::MakeGridObject(0, 0, 0, kBlockIdSolid, 0));
    std::vector<NS::Game::Level::BlockEntry> blocks;
    blocks.push_back({1, 0, 0, kBlockIdSlope45, 1, 0});
    blocks.push_back({2, 0, 0, kBlockIdCoin, 0, 0});
    NS::Game::Level::MigrateBlocksToObjects(level, blocks);

    // 移行前は全 object が components 空 + kind (LegacyKind placeholder すら持たない)
    ASSERT_EQ(level.objects.size(), 3u);
    for (const auto& object : level.objects)
        ASSERT_TRUE(object.components.empty());

    NS::Scene::AssetManager assets{std::filesystem::path{"."}};
    const std::vector<std::string> noPaths;

    // 移行前の kind 駆動 build の signature を退避する
    std::vector<ColliderSignature> beforeSigs;
    for (const auto& object : level.objects)
    {
        auto built = BuildPlacedObject(object, assets, noPaths);
        ASSERT_NE(built, nullptr);
        beforeSigs.push_back(ExtractColliderSignature(*built));
    }

    MigrateLegacyLevel(level);

    // 移行後は全 object が実 component を持ち LegacyKind は残らない
    for (const auto& object : level.objects)
    {
        EXPECT_FALSE(object.components.empty());
        for (const auto& comp : object.components)
            EXPECT_NE(comp.typeName, "LegacyKind");
    }

    // 二重 migrate しても各 object の構成 (component 一覧 + スカラ) が変わらない
    LevelData twice = level;
    MigrateLegacyLevel(twice);
    ASSERT_EQ(twice.objects.size(), level.objects.size());
    for (std::size_t i = 0; i < level.objects.size(); ++i)
        EXPECT_TRUE(twice.objects[i] == level.objects[i]);

    // 移行後の build が移行前 kind 駆動 build と同一 collider channel
    for (std::size_t i = 0; i < level.objects.size(); ++i)
    {
        auto built = BuildPlacedObject(level.objects[i], assets, noPaths);
        ASSERT_NE(built, nullptr);
        ExpectSignatureEqual(beforeSigs[i], ExtractColliderSignature(*built));
    }
}

// 旧 kind JSON の読込移行を JSON 往復しても、 実 component 一覧と collider channel が安定する (移行の不動点)
// 端 A: 旧形式 → 読込 → 移行。 端 B: 端 A を直列化 → 再読込 → 再移行。 両端が同じ構成 / 同じ当たりを組む
TEST(BehaviorZero, LegacyKindMigrationRoundTripIsStable)
{
    const std::vector<LegacyKindCase> cases = {{kBlockIdSolid, 0.0f},
                                               {kBlockIdSlope45, 1.0f},
                                               {kBlockIdPole, 2.0f},
                                               {kBlockIdHazard, 3.0f},
                                               {kBlockIdWater, 4.0f},
                                               {kBlockIdDecoration, 5.0f},
                                               {kBlockIdCoin, 6.0f},
                                               {kBlockIdPowerStar, 7.0f}};

    LevelData endA;
    ASSERT_TRUE(DeserializeLevelFromJson(endA, MakeLegacyLevelJson(cases)));
    MigrateLegacyLevel(endA);

    // 端 A を直列化して再読込・再移行する。 実 component なので 2 度目の移行は何もしない
    LevelData endB;
    ASSERT_TRUE(DeserializeLevelFromJson(endB, SerializeLevelToJson(endA)));
    MigrateLegacyLevel(endB);

    ASSERT_EQ(endA.objects.size(), cases.size());
    ASSERT_EQ(endB.objects.size(), cases.size());

    // 正準直列化の一致は typeName 集合 + 全 FieldValue が往復後も安定していることを示す (順序非依存)
    EXPECT_EQ(SerializeLevelToJson(endA), SerializeLevelToJson(endB));

    NS::Scene::AssetManager assets{std::filesystem::path{"."}};
    const std::vector<std::string> noPaths;

    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        for (const auto& comp : endA.objects[i].components)
            EXPECT_NE(comp.typeName, "LegacyKind");
        for (const auto& comp : endB.objects[i].components)
            EXPECT_NE(comp.typeName, "LegacyKind");

        auto builtA = BuildPlacedObject(endA.objects[i], assets, noPaths);
        auto builtB = BuildPlacedObject(endB.objects[i], assets, noPaths);
        ASSERT_NE(builtA, nullptr);
        ASSERT_NE(builtB, nullptr);

        if (cases[i].kind == kBlockIdCoin || cases[i].kind == kBlockIdPowerStar)
        {
            // coin / star は PickupComponent のみ・ MeshRenderer 無し (視覚ゼロ) を両端で維持する
            EXPECT_EQ(FindComponent<NS::Scene::MeshRendererComponent>(*builtA), nullptr);
            EXPECT_EQ(FindComponent<NS::Scene::MeshRendererComponent>(*builtB), nullptr);
            EXPECT_NE(FindComponent<NS::Scene::PickupComponent>(*builtA), nullptr);
            EXPECT_NE(FindComponent<NS::Scene::PickupComponent>(*builtB), nullptr);
        }

        ExpectSignatureEqual(ExtractColliderSignature(*builtA), ExtractColliderSignature(*builtB));
    }
}
