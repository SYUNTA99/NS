#include "Game/Level/BlockObject.h"
#include "Game/Player.h"

#include <Editor/EditorObjects.h>
#include <Game/Level/GoalComponent.h>
#include <Runtime/Object/AssetManager.h>
#include <Runtime/Object/Components/BoxColliderComponent.h>
#include <Runtime/Object/Components/CapsuleColliderComponent.h>
#include <Game/Player/PlayerComponent.h>
#include <Runtime/Object/Components/MeshRendererComponent.h>
#include <Runtime/Object/Components/SlopeColliderComponent.h>
#include <Runtime/Object/Components/SphereColliderComponent.h>
#include <Runtime/Object/Components/TransformComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Reflection/ObjectBuilder.h>
#include <Runtime/Object/Reflection/ReflectionJson.h>
#include <Runtime/Object/Reflection/TypeRegistry.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <string_view>
#include <vector>

namespace
{
    using NS::Game::Level::MakeCellCubeComponents;
    using NS::Game::Level::MakeCellObject;
    using NS::Object::ObjectData;
    using NS::Core::Vector3;

    // device を確立しない AssetManager。 Builtin / SharedMaterial は nullptr を返すが、 汎用構築は落ちない
    class ObjectBuildTest : public ::testing::Test
    {
    protected:
        NS::Object::AssetManager m_assets{std::filesystem::path{"."}};

        std::unique_ptr<NS::Object::GameObject> Build(const ObjectData& object)
        {
            return NS::Object::BuildSceneObject(object, &m_assets);
        }
    };

    // grid に置く素の cube
    ObjectData MakeGridCube()
    {
        return MakeCellObject(0, 0, 0);
    }

    // 自由配置物のデータ。 見た目の cube と渡された collider を component 直書きで積む
    ObjectData MakeFreeObject(nlohmann::json collider)
    {
        ObjectData object;
        nlohmann::json mesh = NS::Object::MakeComponentEntry("MeshRendererComponent");
        NS::Object::SetField(mesh, "メッシュ", "cube");
        object.components.push_back(std::move(mesh));
        object.components.push_back(std::move(collider));
        return object;
    }

    nlohmann::json BoxColliderData(const Vector3& half)
    {
        nlohmann::json data = NS::Object::MakeComponentEntry("BoxColliderComponent");
        NS::Object::SetField(data, "半径", half);
        return data;
    }

    nlohmann::json SphereColliderData(float radius, const Vector3& offset)
    {
        nlohmann::json data = NS::Object::MakeComponentEntry("SphereColliderComponent");
        NS::Object::SetField(data, "半径", radius);
        NS::Object::SetField(data, "中心オフセット", offset);
        return data;
    }

    nlohmann::json CapsuleColliderData(float radius, float halfHeight)
    {
        nlohmann::json data = NS::Object::MakeComponentEntry("CapsuleColliderComponent");
        NS::Object::SetField(data, "半径", radius);
        NS::Object::SetField(data, "半分の高さ", halfHeight);
        return data;
    }

    template <class T> bool Has(NS::Object::GameObject& obj)
    {
        return obj.FindComponent<T>() != nullptr;
    }
} // namespace

TEST_F(ObjectBuildTest, GridCubeHasMeshAndBoxCollider)
{
    auto obj = Build(MakeGridCube());
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(Has<NS::Object::MeshRendererComponent>(*obj));
    EXPECT_TRUE(Has<NS::Object::BoxColliderComponent>(*obj));
    EXPECT_FALSE(Has<NS::Object::SphereColliderComponent>(*obj));
    EXPECT_FALSE(Has<NS::Object::CapsuleColliderComponent>(*obj));
}

TEST_F(ObjectBuildTest, DefaultFreeCubeComponentsAreCubeWithBoxCollider)
{
    ObjectData object;
    object.components = MakeCellCubeComponents();

    auto obj = Build(object);
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(Has<NS::Object::MeshRendererComponent>(*obj));
    auto* box = obj->FindComponent<NS::Object::BoxColliderComponent>();
    ASSERT_NE(box, nullptr);
    const Vector3 half = box->HalfExtents();
    EXPECT_FLOAT_EQ(half.x, 0.5f);
    EXPECT_FLOAT_EQ(half.y, 0.5f);
    EXPECT_FLOAT_EQ(half.z, 0.5f);
}

TEST_F(ObjectBuildTest, FreeBoxHasBoxColliderWithSavedHalfExtents)
{
    auto obj = Build(MakeFreeObject(BoxColliderData(Vector3{1.0f, 2.0f, 3.0f})));
    ASSERT_NE(obj, nullptr);
    auto* box = obj->FindComponent<NS::Object::BoxColliderComponent>();
    ASSERT_NE(box, nullptr);
    const Vector3 half = box->HalfExtents();
    EXPECT_FLOAT_EQ(half.x, 1.0f);
    EXPECT_FLOAT_EQ(half.y, 2.0f);
    EXPECT_FLOAT_EQ(half.z, 3.0f);
    EXPECT_FALSE(Has<NS::Object::SphereColliderComponent>(*obj));
    EXPECT_FALSE(Has<NS::Object::CapsuleColliderComponent>(*obj));
}

TEST_F(ObjectBuildTest, FreeSphereHasOnlySphereCollider)
{
    auto obj = Build(MakeFreeObject(SphereColliderData(0.7f, Vector3{0.0f, 1.0f, 0.0f})));
    ASSERT_NE(obj, nullptr);
    EXPECT_FALSE(Has<NS::Object::BoxColliderComponent>(*obj));
    auto* sphere = obj->FindComponent<NS::Object::SphereColliderComponent>();
    ASSERT_NE(sphere, nullptr);
    EXPECT_FALSE(Has<NS::Object::CapsuleColliderComponent>(*obj));

    const NS::Core::Sphere world = sphere->WorldSphere();
    EXPECT_NEAR(world.radius, 0.7f, 1e-4f);
    EXPECT_NEAR(world.center.y, 1.0f, 1e-4f);
}

TEST_F(ObjectBuildTest, SphereColliderWorldAabbEnclosesSphere)
{
    auto obj = Build(MakeFreeObject(SphereColliderData(0.7f, Vector3{0.0f, 1.0f, 0.0f})));
    ASSERT_NE(obj, nullptr);
    auto* sphere = obj->FindComponent<NS::Object::SphereColliderComponent>();
    ASSERT_NE(sphere, nullptr);
    const NS::Core::AABB aabb = sphere->WorldAABB();
    EXPECT_NEAR(aabb.Center.y, 1.0f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.x, 0.7f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.y, 0.7f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.z, 0.7f, 1e-4f);
}

TEST_F(ObjectBuildTest, FreeCapsuleHasOnlyCapsuleCollider)
{
    auto obj = Build(MakeFreeObject(CapsuleColliderData(0.4f, 0.9f)));
    ASSERT_NE(obj, nullptr);
    EXPECT_FALSE(Has<NS::Object::BoxColliderComponent>(*obj));
    auto* capsule = obj->FindComponent<NS::Object::CapsuleColliderComponent>();
    ASSERT_NE(capsule, nullptr);
    EXPECT_FALSE(Has<NS::Object::SphereColliderComponent>(*obj));

    const NS::Physics::Capsule world = capsule->WorldCapsule();
    EXPECT_NEAR(world.radius, 0.4f, 1e-4f);
    EXPECT_NEAR(world.halfHeight, 0.9f, 1e-4f);
}

TEST_F(ObjectBuildTest, CapsuleColliderWorldAabbEnclosesCapsule)
{
    auto obj = Build(MakeFreeObject(CapsuleColliderData(0.4f, 0.9f)));
    ASSERT_NE(obj, nullptr);
    auto* capsule = obj->FindComponent<NS::Object::CapsuleColliderComponent>();
    ASSERT_NE(capsule, nullptr);
    const NS::Core::AABB aabb = capsule->WorldAABB();
    EXPECT_NEAR(aabb.Extents.x, 0.4f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.y, 1.3f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.z, 0.4f, 1e-4f);
}

TEST_F(ObjectBuildTest, TransformAppliedToRoot)
{
    ObjectData object = MakeGridCube();
    NS::Object::SetObjectPosition(object, Vector3{3.0f, 4.0f, 5.0f});

    auto obj = Build(object);
    ASSERT_NE(obj, nullptr);
    const Vector3 pos = obj->Root().Position();
    EXPECT_FLOAT_EQ(pos.x, 3.0f);
    EXPECT_FLOAT_EQ(pos.y, 4.0f);
    EXPECT_FLOAT_EQ(pos.z, 5.0f);
}

TEST_F(ObjectBuildTest, GridCubeWorldAabbMatchesCellHalfExtents)
{
    auto obj = Build(MakeGridCube());
    ASSERT_NE(obj, nullptr);
    auto* box = obj->FindComponent<NS::Object::BoxColliderComponent>();
    ASSERT_NE(box, nullptr);
    const NS::Core::AABB aabb = box->WorldAABB();
    EXPECT_NEAR(aabb.Center.x, 0.0f, 1e-4f);
    EXPECT_NEAR(aabb.Center.y, 0.0f, 1e-4f);
    EXPECT_NEAR(aabb.Center.z, 0.0f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.x, 0.5f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.y, 0.5f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.z, 0.5f, 1e-4f);
}

TEST_F(ObjectBuildTest, FreeBoxWorldAabbReflectsPositionAndHalfExtents)
{
    ObjectData object = MakeFreeObject(BoxColliderData(Vector3{1.0f, 2.0f, 3.0f}));
    NS::Object::SetObjectPosition(object, Vector3{2.0f, 0.0f, 0.0f});

    auto obj = Build(object);
    ASSERT_NE(obj, nullptr);
    auto* box = obj->FindComponent<NS::Object::BoxColliderComponent>();
    ASSERT_NE(box, nullptr);
    const NS::Core::AABB aabb = box->WorldAABB();
    EXPECT_NEAR(aabb.Center.x, 2.0f, 1e-4f);
    EXPECT_NEAR(aabb.Center.y, 0.0f, 1e-4f);
    EXPECT_NEAR(aabb.Center.z, 0.0f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.x, 1.0f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.y, 2.0f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.z, 3.0f, 1e-4f);
}

// components 一覧を持つ object は registry でコンポを生成し、 リフレクション set でフィールドが入る
TEST_F(ObjectBuildTest, ComponentsDriveBuild)
{
    ObjectData object;
    object.components.push_back(BoxColliderData(Vector3{1.0f, 2.0f, 3.0f}));

    auto obj = Build(object);
    ASSERT_NE(obj, nullptr);
    auto* boxComp = obj->FindComponent<NS::Object::BoxColliderComponent>();
    ASSERT_NE(boxComp, nullptr);
    const Vector3 half = boxComp->HalfExtents();
    EXPECT_FLOAT_EQ(half.x, 1.0f);
    EXPECT_FLOAT_EQ(half.y, 2.0f);
    EXPECT_FLOAT_EQ(half.z, 3.0f);
}

// components を持たない object は配置物として組まれず nullptr が返る
TEST_F(ObjectBuildTest, EmptyComponentsBuildsNothing)
{
    ObjectData object;
    ASSERT_TRUE(object.components.empty());

    EXPECT_EQ(Build(object), nullptr);
}

// material 参照に .. を含む値は ContentRoot 外解決を拒否し、 共有の代替に切り替わってクラッシュしない
TEST_F(ObjectBuildTest, AssetPathTraversalRejectedFallsBackToDefault)
{
    ObjectData object = MakeFreeObject(BoxColliderData(Vector3{0.5f, 0.5f, 0.5f}));
    NS::Object::SetField(object.components[0], "マテリアル", "../evil.mat");

    // const char* が string_view 版へ解決され、 bool でなく文字列として書かれていること
    ASSERT_TRUE(object.components[0]["fields"]["マテリアル"].is_string());

    auto obj = Build(object);
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(Has<NS::Object::MeshRendererComponent>(*obj));
    EXPECT_TRUE(Has<NS::Object::BoxColliderComponent>(*obj));
}

// 45 度スロープ prototype は wedge メッシュ + 45 度 SlopeCollider を作り、 R で回せる
TEST_F(ObjectBuildTest, GridSlopeHasSlopeColliderAndDisplaysAsSlope45)
{
    ObjectData slope = MakeCellObject(0, 0, 0);
    slope.components = NS::Editor::MakeCellSlopeComponents(45.0f);

    EXPECT_STREQ(NS::Editor::ObjectDisplayName(slope), "Slope 45");
    EXPECT_TRUE(NS::Editor::IsRotatableObject(slope));

    auto obj = Build(slope);
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(Has<NS::Object::MeshRendererComponent>(*obj));
    auto* collider = obj->FindComponent<NS::Object::SlopeColliderComponent>();
    ASSERT_NE(collider, nullptr);
    EXPECT_FLOAT_EQ(collider->AngleDegrees(), 45.0f);
    EXPECT_FALSE(Has<NS::Object::BoxColliderComponent>(*obj));
}

// ゴール prototype は接触クリアの印を持ち、 表示名は Goal、 向きは無関係で回転不可
TEST_F(ObjectBuildTest, GoalHasMarkerAndDisplaysAsGoal)
{
    ObjectData goal = MakeCellObject(0, 0, 0);
    goal.components = NS::Editor::MakeGoalComponents();

    EXPECT_STREQ(NS::Editor::ObjectDisplayName(goal), "Goal");
    EXPECT_FALSE(NS::Editor::IsRotatableObject(goal));

    auto obj = Build(goal);
    ASSERT_NE(obj, nullptr);
    EXPECT_NE(obj->FindComponent<NS::Game::Level::GoalComponent>(), nullptr);
}

// grid に置く cube は固形なので R で 90° 回せる
TEST_F(ObjectBuildTest, GridCubeIsRotatable)
{
    EXPECT_TRUE(NS::Editor::IsRotatableObject(MakeCellObject(0, 0, 0)));
}

// 自由配置の cube も固形 box なので回せる。 固形判定には BoxCollider が要るので、 空構成の marker は回せない
TEST_F(ObjectBuildTest, FreeCubeRotatableButEmptyMarkerNot)
{
    ObjectData freeCube;
    freeCube.components = MakeCellCubeComponents();
    EXPECT_TRUE(NS::Editor::IsRotatableObject(freeCube));

    ObjectData marker;
    EXPECT_FALSE(NS::Editor::IsRotatableObject(marker));
}

// プレイヤー実体は className から Player 派生の GameObject に二重生成なしで組まれる
TEST_F(ObjectBuildTest, PlayerObjectBuildsPlayerTyped)
{
    const ObjectData data = MakePlayerObject(Vector3{1.0f, 2.0f, 3.0f}, NS::Core::Quaternion{});
    EXPECT_EQ(data.className, "Player");

    auto obj = Build(data);
    ASSERT_NE(obj, nullptr);
    // 実行時型情報は切っているため、クラス名で型選択を確かめてから GameObject の API で見る
    ASSERT_STREQ(obj->ClassName(), "Player");

    // コンストラクタが積む分と同じ数。 transform も GameObject が持つので二重にはならない
    EXPECT_EQ(obj->Components().size(), Player{}.Components().size());

    // pose は他の配置物と同じく data から乗る
    EXPECT_FLOAT_EQ(obj->Root().Position().y, 2.0f);
}

// プレイヤーのデータ構成は Player のコンストラクタから吸い出した型名だけの疎な一覧。 値は書かない
TEST_F(ObjectBuildTest, PlayerObjectDataIsSparseTypeListFromClass)
{
    const ObjectData data = MakePlayerObject(Vector3{}, NS::Core::Quaternion{});

    ASSERT_EQ(data.components.size(), Player{}.Components().size());
    EXPECT_NE(NS::Object::FindComponentEntry(data, "MeshRendererComponent"), nullptr);
    EXPECT_NE(NS::Object::FindComponentEntry(data, "PlayerComponent"), nullptr);
    EXPECT_NE(NS::Object::FindComponentEntry(data, "PlayerStatsManagerComponent"), nullptr);
    EXPECT_NE(NS::Object::FindComponentEntry(data, "PlayerStateManagerComponent"), nullptr);
    EXPECT_NE(NS::Object::FindComponentEntry(data, "PlayerInputComponent"), nullptr);
    EXPECT_NE(NS::Object::FindComponentEntry(data, "HealthComponent"), nullptr);
    EXPECT_NE(NS::Object::FindComponentEntry(data, "ShadowComponent"), nullptr);
    for (const nlohmann::json& entry : data.components)
    {
        const std::string_view typeName = NS::Object::ComponentEntryType(entry);
        if (typeName == "TransformComponent")
            continue; // transform だけは値を持つ
        EXPECT_TRUE(entry.at("fields").empty()) << typeName;
    }
    // 種別判定は疎なデータでも成立する
    EXPECT_TRUE(IsPlayerObject(data));
}

// 疎なデータで組んでも、 cube と共有 player 材質と赤の個体色はコンストラクタが与える
TEST_F(ObjectBuildTest, PlayerDefaultLookComesFromClassNotData)
{
    auto obj = Build(MakePlayerObject(Vector3{}, NS::Core::Quaternion{}));
    ASSERT_NE(obj, nullptr);
    auto* mesh = obj->FindComponent<NS::Object::MeshRendererComponent>();
    ASSERT_NE(mesh, nullptr);

    EXPECT_EQ(mesh->MeshRef(), "cube");
    EXPECT_EQ(mesh->MaterialRef(), "player");

    // 個体色は getter が無いのでリフレクションフィールド越しに読む
    const NS::Object::ReflectionInfo* info = NS::Object::MeshRendererComponent::StaticReflection();
    NS::Core::Vector3 baseColor{};
    bool found = false;
    for (std::size_t i = 0; i < info->fieldCount; ++i)
    {
        if (std::string_view{info->fields[i].name} == "基本色")
        {
            info->fields[i].get(mesh, &baseColor);
            found = true;
        }
    }
    ASSERT_TRUE(found);
    EXPECT_FLOAT_EQ(baseColor.x, 0.85f);
    EXPECT_FLOAT_EQ(baseColor.y, 0.20f);
    EXPECT_FLOAT_EQ(baseColor.z, 0.20f);
}

// data 側で書き込んだ値が既定構成の component へリフレクション適用される
TEST_F(ObjectBuildTest, PlayerObjectAppliesDataValuesToComponents)
{
    ObjectData data = MakePlayerObject(Vector3{}, NS::Core::Quaternion{});
    for (nlohmann::json& entry : data.components)
        if (NS::Object::ComponentEntryType(entry) == "PlayerStatsManagerComponent")
            NS::Object::SetField(entry, "コヨーテ時間", 0.125f);

    auto obj = Build(data);
    ASSERT_NE(obj, nullptr);
    obj->OnStart();
    auto* movement = obj->FindComponent<NS::Game::Player::PlayerComponent>();
    ASSERT_NE(movement, nullptr);
    EXPECT_FLOAT_EQ(movement->CoyoteTime(), 0.125f);
}

// 同型 component を重ねたデータは live でも同数立ち、 2 件目が 1 件目へ上書きされない
// 当たりの重ね置きは複合形状として衝突へ効く前提の機能で、 貼り重ねの経路がこの形を作る
TEST_F(ObjectBuildTest, DuplicateColliderDataBuildsCompoundColliders)
{
    ObjectData object = MakeFreeObject(BoxColliderData(Vector3{1.0f, 1.0f, 1.0f}));
    object.components.push_back(BoxColliderData(Vector3{2.0f, 2.0f, 2.0f}));

    auto obj = Build(object);
    ASSERT_NE(obj, nullptr);

    std::vector<NS::Object::BoxColliderComponent*> boxes;
    for (NS::Object::Component* comp : obj->Components())
        if (auto* box = NS::Object::ComponentCast<NS::Object::BoxColliderComponent>(comp))
            boxes.push_back(box);
    ASSERT_EQ(boxes.size(), 2u);
    EXPECT_FLOAT_EQ(boxes[0]->HalfExtents().x, 1.0f);
    EXPECT_FLOAT_EQ(boxes[1]->HalfExtents().x, 2.0f);
}

// 実体をリフレクション serialize → 既定 component へ apply → 再 serialize で一致する
// 保存を data でなく実体から作る前提。 registry で組める component だけを対象にする
TEST_F(ObjectBuildTest, LiveComponentsSerializeRoundTripFaithfully)
{
    ObjectData object = MakeFreeObject(BoxColliderData(Vector3{1.0f, 2.0f, 3.0f}));
    NS::Object::SetObjectPosition(object, Vector3{3.0f, 4.0f, 5.0f});
    NS::Object::SetObjectScale(object, Vector3{2.0f, 1.0f, 0.5f});

    auto obj = Build(object);
    ASSERT_NE(obj, nullptr);

    const nlohmann::json components = NS::Object::SerializeGameObjectComponents(*obj);
    ASSERT_EQ(components.size(), obj->Components().size());

    // 各 component を型名から既定生成し、 serialize した fields を書き戻して再 serialize が元に戻るか見る
    NS::Object::GameObject rebuilt;
    for (const auto& compJson : components)
    {
        const std::string typeName = compJson.at("type").get<std::string>();
        // transform は登録一覧に無く GameObject が最初から 1 つ持っているので、 生成せずそれへ書き戻す
        NS::Object::Component* fresh = nullptr;
        if (typeName == NS::Object::k_TransformTypeName)
            fresh = rebuilt.FindComponent<NS::Object::TransformComponent>();
        else
            fresh = NS::Object::CreateComponent(typeName, rebuilt);
        ASSERT_NE(fresh, nullptr) << typeName;
        NS::Object::ApplyJsonFields(*fresh, compJson.at("fields"));
        EXPECT_EQ(NS::Object::SerializeComponent(*fresh), compJson) << typeName;
    }
}

// 実体を CaptureObjectData で値データへ忠実に写し、 素の GameObject へ ApplyObjectComponents で復元すると
// 元 live と component JSON が一致する。 undo とプレイ↔編集の退避・復元に使う機構を直接確かめる
TEST_F(ObjectBuildTest, CaptureObjectDataRestoresFaithfully)
{
    ObjectData object = MakeFreeObject(BoxColliderData(Vector3{1.0f, 2.0f, 3.0f}));
    NS::Object::SetObjectPosition(object, Vector3{3.0f, 4.0f, 5.0f});
    NS::Object::SetObjectScale(object, Vector3{2.0f, 1.0f, 0.5f});

    auto live = Build(object);
    ASSERT_NE(live, nullptr);

    const ObjectData snapshot = NS::Object::CaptureObjectData(*live);
    ASSERT_EQ(snapshot.components.size(), live->Components().size());

    NS::Object::GameObject restored;
    NS::Object::ApplyObjectComponents(restored, snapshot, {});
    EXPECT_EQ(NS::Object::SerializeGameObjectComponents(restored), NS::Object::SerializeGameObjectComponents(*live));
}

// データの active は保存の往復で残り、 読み直した実体にも false のまま乗る
TEST_F(ObjectBuildTest, DisabledComponentSurvivesRoundTrip)
{
    ObjectData object = MakeFreeObject(BoxColliderData(Vector3{0.5f, 0.5f, 0.5f}));
    NS::Object::SetComponentEntryEnabled(object.components[0], false);

    auto live = Build(object);
    ASSERT_NE(live, nullptr);
    auto* mesh = live->FindComponent<NS::Object::MeshRendererComponent>();
    ASSERT_NE(mesh, nullptr);
    EXPECT_FALSE(mesh->IsEnabled());

    const ObjectData snapshot = NS::Object::CaptureObjectData(*live);
    const nlohmann::json* entry = NS::Object::FindComponentEntry(snapshot, "MeshRendererComponent");
    ASSERT_NE(entry, nullptr);
    EXPECT_FALSE(NS::Object::ComponentEntryEnabled(*entry));
}

// モード切替で休止させただけの component は保存に持ち込まない。 書き込むと読み直しでも動かなくなる
TEST_F(ObjectBuildTest, SleepingComponentIsSavedAsEnabled)
{
    ObjectData object = MakeFreeObject(BoxColliderData(Vector3{0.5f, 0.5f, 0.5f}));

    auto live = Build(object);
    ASSERT_NE(live, nullptr);
    auto* mesh = live->FindComponent<NS::Object::MeshRendererComponent>();
    ASSERT_NE(mesh, nullptr);

    mesh->SetActive(false);

    const ObjectData snapshot = NS::Object::CaptureObjectData(*live);
    const nlohmann::json* entry = NS::Object::FindComponentEntry(snapshot, "MeshRendererComponent");
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(NS::Object::ComponentEntryEnabled(*entry));
}
