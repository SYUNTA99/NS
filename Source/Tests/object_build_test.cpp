#include "Game/Level/BlockObject.h"
#include "Game/Player.h"
#include "tuning_field_access.h"

#include <Editor/EditorObjects.h>
#include <Game/Level/GoalComponent.h>
#include <Game/Player/PlayerComponent.h>
#include <Runtime/Core/AABB.h>
#include <Runtime/Core/Sphere.h>
#include <Runtime/Object/AssetManager.h>
#include <Runtime/Object/Components/BoxColliderComponent.h>
#include <Runtime/Object/Components/CapsuleColliderComponent.h>
#include <Runtime/Object/Components/MeshRendererComponent.h>
#include <Runtime/Object/Components/SlopeColliderComponent.h>
#include <Runtime/Object/Components/SphereColliderComponent.h>
#include <Runtime/Object/Components/TransformComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Reflection/ObjectBuilder.h>
#include <Runtime/Object/Reflection/ReflectionJson.h>
#include <Runtime/Object/Reflection/TypeRegistry.h>
#include <cstdint>
#include <gtest/gtest.h>
#include <string_view>
#include <vector>

namespace
{
    using NS::Game::Level::MakeCellCubeComponents;
    using NS::Game::Level::MakeCellObject;
    using NS::Obj::ObjectData;
    using NS::Core::Vector3;

    // RegisterBuiltins も RegisterSharedMaterials も呼ばない AssetManager。組み込みと共有材質は
    // nullptr を返すが、汎用構築は落ちない
    class ObjectBuildTest : public ::testing::Test
    {
    protected:
        NS::Obj::AssetManager m_assets{std::string{"."}};

        std::unique_ptr<NS::Obj::GameObject> Build(const ObjectData& object)
        {
            return NS::Obj::BuildSceneObject(object, &m_assets);
        }
    };

    // 格子に置く素の cube
    ObjectData MakeGridCube()
    {
        return MakeCellObject(0, 0, 0);
    }

    // 自由配置物のデータ。見た目の cube と渡された collider を component 直書きで積む
    ObjectData MakeFreeObject(nlohmann::json collider)
    {
        ObjectData object;
        nlohmann::json mesh = NS::Obj::MakeComponentEntry("MeshRendererComponent");
        NS::Obj::SetField(mesh, "メッシュ", "cube");
        object.components.push_back(std::move(mesh));
        object.components.push_back(std::move(collider));
        return object;
    }

    nlohmann::json BoxColliderData(const Vector3& half)
    {
        nlohmann::json data = NS::Obj::MakeComponentEntry("BoxColliderComponent");
        NS::Obj::SetField(data, "半径", half);
        return data;
    }

    nlohmann::json SphereColliderData(float radius, const Vector3& offset)
    {
        nlohmann::json data = NS::Obj::MakeComponentEntry("SphereColliderComponent");
        NS::Obj::SetField(data, "半径", radius);
        NS::Obj::SetField(data, "中心オフセット", offset);
        return data;
    }

    nlohmann::json CapsuleColliderData(float radius, float halfHeight)
    {
        nlohmann::json data = NS::Obj::MakeComponentEntry("CapsuleColliderComponent");
        NS::Obj::SetField(data, "半径", radius);
        NS::Obj::SetField(data, "半分の高さ", halfHeight);
        return data;
    }

    template <class T> bool Has(NS::Obj::GameObject& obj)
    {
        return obj.FindComponent<T>() != nullptr;
    }
} // namespace

TEST_F(ObjectBuildTest, GridCubeHasMeshAndBoxCollider)
{
    auto obj = Build(MakeGridCube());
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(Has<NS::Obj::MeshRendererComponent>(*obj));
    EXPECT_TRUE(Has<NS::Obj::BoxColliderComponent>(*obj));
    EXPECT_FALSE(Has<NS::Obj::SphereColliderComponent>(*obj));
    EXPECT_FALSE(Has<NS::Obj::CapsuleColliderComponent>(*obj));
}

TEST_F(ObjectBuildTest, DefaultFreeCubeComponentsAreCubeWithBoxCollider)
{
    ObjectData object;
    object.components = MakeCellCubeComponents();

    auto obj = Build(object);
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(Has<NS::Obj::MeshRendererComponent>(*obj));
    auto* box = obj->FindComponent<NS::Obj::BoxColliderComponent>();
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
    auto* box = obj->FindComponent<NS::Obj::BoxColliderComponent>();
    ASSERT_NE(box, nullptr);
    const Vector3 half = box->HalfExtents();
    EXPECT_FLOAT_EQ(half.x, 1.0f);
    EXPECT_FLOAT_EQ(half.y, 2.0f);
    EXPECT_FLOAT_EQ(half.z, 3.0f);
    EXPECT_FALSE(Has<NS::Obj::SphereColliderComponent>(*obj));
    EXPECT_FALSE(Has<NS::Obj::CapsuleColliderComponent>(*obj));
}

TEST_F(ObjectBuildTest, FreeSphereHasOnlySphereCollider)
{
    auto obj = Build(MakeFreeObject(SphereColliderData(0.7f, Vector3{0.0f, 1.0f, 0.0f})));
    ASSERT_NE(obj, nullptr);
    EXPECT_FALSE(Has<NS::Obj::BoxColliderComponent>(*obj));
    auto* sphere = obj->FindComponent<NS::Obj::SphereColliderComponent>();
    ASSERT_NE(sphere, nullptr);
    EXPECT_FALSE(Has<NS::Obj::CapsuleColliderComponent>(*obj));

    const NS::Core::Sphere world = sphere->WorldSphere();
    EXPECT_NEAR(world.radius, 0.7f, 1e-4f);
    EXPECT_NEAR(world.center.y, 1.0f, 1e-4f);
}

TEST_F(ObjectBuildTest, SphereColliderWorldAabbEnclosesSphere)
{
    auto obj = Build(MakeFreeObject(SphereColliderData(0.7f, Vector3{0.0f, 1.0f, 0.0f})));
    ASSERT_NE(obj, nullptr);
    auto* sphere = obj->FindComponent<NS::Obj::SphereColliderComponent>();
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
    EXPECT_FALSE(Has<NS::Obj::BoxColliderComponent>(*obj));
    auto* capsule = obj->FindComponent<NS::Obj::CapsuleColliderComponent>();
    ASSERT_NE(capsule, nullptr);
    EXPECT_FALSE(Has<NS::Obj::SphereColliderComponent>(*obj));

    const NS::Phys::Capsule worldCapsule = capsule->WorldCapsule();
    EXPECT_NEAR(worldCapsule.radius, 0.4f, 1e-4f);
    EXPECT_NEAR(worldCapsule.halfHeight, 0.9f, 1e-4f);
}

TEST_F(ObjectBuildTest, CapsuleColliderWorldAabbEnclosesCapsule)
{
    auto obj = Build(MakeFreeObject(CapsuleColliderData(0.4f, 0.9f)));
    ASSERT_NE(obj, nullptr);
    auto* capsule = obj->FindComponent<NS::Obj::CapsuleColliderComponent>();
    ASSERT_NE(capsule, nullptr);
    const NS::Core::AABB aabb = capsule->WorldAABB();
    EXPECT_NEAR(aabb.Extents.x, 0.4f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.y, 1.3f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.z, 0.4f, 1e-4f);
}

TEST_F(ObjectBuildTest, TransformAppliedToRoot)
{
    ObjectData object = MakeGridCube();
    NS::Obj::SetObjectPosition(object, Vector3{3.0f, 4.0f, 5.0f});

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
    auto* box = obj->FindComponent<NS::Obj::BoxColliderComponent>();
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
    NS::Obj::SetObjectPosition(object, Vector3{2.0f, 0.0f, 0.0f});

    auto obj = Build(object);
    ASSERT_NE(obj, nullptr);
    auto* box = obj->FindComponent<NS::Obj::BoxColliderComponent>();
    ASSERT_NE(box, nullptr);
    const NS::Core::AABB aabb = box->WorldAABB();
    EXPECT_NEAR(aabb.Center.x, 2.0f, 1e-4f);
    EXPECT_NEAR(aabb.Center.y, 0.0f, 1e-4f);
    EXPECT_NEAR(aabb.Center.z, 0.0f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.x, 1.0f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.y, 2.0f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.z, 3.0f, 1e-4f);
}

// components 一覧を持つ object は registry で component を生成し、リフレクション set でフィールドが入る
TEST_F(ObjectBuildTest, ComponentsDriveBuild)
{
    ObjectData object;
    object.components.push_back(BoxColliderData(Vector3{1.0f, 2.0f, 3.0f}));

    auto obj = Build(object);
    ASSERT_NE(obj, nullptr);
    auto* boxComp = obj->FindComponent<NS::Obj::BoxColliderComponent>();
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

// 材質の参照が .. で ContentRoot の外へ出るなら拒否する。落ちずに component が揃うことだけを見る
TEST_F(ObjectBuildTest, AssetPathTraversalRejectedFallsBackToDefault)
{
    ObjectData object = MakeFreeObject(BoxColliderData(Vector3{0.5f, 0.5f, 0.5f}));
    NS::Obj::SetField(object.components[0], "マテリアル", "../evil.mat");

    // const char* が string_view 版へ解決され、bool でなく文字列として書かれていること
    ASSERT_TRUE(object.components[0]["fields"]["マテリアル"].is_string());

    auto obj = Build(object);
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(Has<NS::Obj::MeshRendererComponent>(*obj));
    EXPECT_TRUE(Has<NS::Obj::BoxColliderComponent>(*obj));
}

// 45 度スロープの雛形は表示名が Slope 45 で、45 度の SlopeCollider を持ち、回せる
TEST_F(ObjectBuildTest, GridSlopeHasSlopeColliderAndDisplaysAsSlope45)
{
    ObjectData slope = MakeCellObject(0, 0, 0);
    slope.components = NS::Editor::MakeCellSlopeComponents(45.0f);

    EXPECT_STREQ(NS::Editor::ObjectDisplayName(slope), "Slope 45");
    EXPECT_TRUE(NS::Editor::IsRotatableObject(slope));

    auto obj = Build(slope);
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(Has<NS::Obj::MeshRendererComponent>(*obj));
    auto* collider = obj->FindComponent<NS::Obj::SlopeColliderComponent>();
    ASSERT_NE(collider, nullptr);
    EXPECT_FLOAT_EQ(collider->AngleDegrees(), 45.0f);
    EXPECT_FALSE(Has<NS::Obj::BoxColliderComponent>(*obj));
}

// ゴールの雛形は接触クリアの印を持ち、表示名は Goal。BoxCollider も SlopeCollider も積まないので回せない
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

// 格子に置く cube は固形なので回せる
TEST_F(ObjectBuildTest, GridCubeIsRotatable)
{
    EXPECT_TRUE(NS::Editor::IsRotatableObject(MakeCellObject(0, 0, 0)));
}

// 自由配置の cube も固形 box なので回せる。固形判定には BoxCollider が要るので、空構成の marker は回せない
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

    // コンストラクタが積む分と同じ数。transform も GameObject が持つので二重にはならない
    EXPECT_EQ(obj->Components().size(), Player{}.Components().size());

    // 姿勢は他の配置物と同じくデータから乗る
    EXPECT_FLOAT_EQ(obj->Root().Position().y, 2.0f);
}

// プレイヤーのデータ構成は Player のコンストラクタから吸い出した型名の一覧。値を持つのは transform だけ
TEST_F(ObjectBuildTest, PlayerObjectDataIsSparseTypeListFromClass)
{
    const ObjectData data = MakePlayerObject(Vector3{}, NS::Core::Quaternion{});

    ASSERT_EQ(data.components.size(), Player{}.Components().size());
    EXPECT_NE(NS::Obj::FindComponentEntry(data, "MeshRendererComponent"), nullptr);
    EXPECT_NE(NS::Obj::FindComponentEntry(data, "PlayerComponent"), nullptr);
    EXPECT_NE(NS::Obj::FindComponentEntry(data, "PlayerStateManagerComponent"), nullptr);
    EXPECT_NE(NS::Obj::FindComponentEntry(data, "PlayerInputComponent"), nullptr);
    EXPECT_NE(NS::Obj::FindComponentEntry(data, "HealthComponent"), nullptr);
    EXPECT_NE(NS::Obj::FindComponentEntry(data, "ShadowComponent"), nullptr);
    for (const nlohmann::json& entry : data.components)
    {
        const std::string_view typeName = NS::Obj::ComponentEntryType(entry);
        if (typeName == "TransformComponent")
            continue; // transform だけは値を持つ
        EXPECT_TRUE(entry.at("fields").empty()) << typeName;
    }
    EXPECT_TRUE(IsPlayerObject(data));
}

// 疎なデータで組んでも、cube と共有 player 材質と赤の個体色はコンストラクタが与える
TEST_F(ObjectBuildTest, PlayerDefaultLookComesFromClassNotData)
{
    auto obj = Build(MakePlayerObject(Vector3{}, NS::Core::Quaternion{}));
    ASSERT_NE(obj, nullptr);
    auto* mesh = obj->FindComponent<NS::Obj::MeshRendererComponent>();
    ASSERT_NE(mesh, nullptr);

    EXPECT_EQ(mesh->MeshRef(), "cube");
    EXPECT_EQ(mesh->MaterialRef(), "player");

    // 個体色は getter が無いのでリフレクションフィールド越しに読む
    const NS::Obj::ReflectionInfo* info = NS::Obj::MeshRendererComponent::StaticReflection();
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
        if (NS::Obj::ComponentEntryType(entry) == "PlayerComponent")
            NS::Obj::SetField(entry, "コヨーテ時間", 0.125f);

    auto obj = Build(data);
    ASSERT_NE(obj, nullptr);
    obj->OnStart();
    auto* movement = obj->FindComponent<NS::Game::Player::PlayerComponent>();
    ASSERT_NE(movement, nullptr);
    EXPECT_FLOAT_EQ(NsTest::ReadTuningField(*movement, "コヨーテ時間"), 0.125f);
}

// Player のデータはコンストラクタが積む型名の一覧なので、どの項目も既存の実体に当たり CreateComponent を通らない
// ApplyObjectComponents が id を書かなくても component の数は合う。数を見る試しでは捕まらない
// ComponentIdSurvivesBuildAndCapture が id を確かめるのは生成された MeshRendererComponent の分だけ
TEST_F(ObjectBuildTest, PlayerObjectAppliesDataIdsToConstructorComponents)
{
    ObjectData data = MakePlayerObject(Vector3{}, NS::Core::Quaternion{});

    nlohmann::json* entry = nullptr;
    for (nlohmann::json& candidate : data.components)
    {
        if (NS::Obj::ComponentEntryType(candidate) == "PlayerComponent")
        {
            entry = &candidate;
        }
    }
    ASSERT_NE(entry, nullptr);

    const std::uint32_t dataId = 4321u;
    NS::Obj::SetComponentEntryId(*entry, dataId);

    auto obj = Build(data);
    ASSERT_NE(obj, nullptr);
    auto* movement = obj->FindComponent<NS::Game::Player::PlayerComponent>();
    ASSERT_NE(movement, nullptr);
    EXPECT_EQ(movement->Id(), dataId);
}

// 同型 component を重ねたデータは live でも同数立ち、2 件目が 1 件目へ上書きされない
// 重ねた当たりは 1 つずつ body になる
TEST_F(ObjectBuildTest, DuplicateColliderDataBuildsCompoundColliders)
{
    ObjectData object = MakeFreeObject(BoxColliderData(Vector3{1.0f, 1.0f, 1.0f}));
    object.components.push_back(BoxColliderData(Vector3{2.0f, 2.0f, 2.0f}));

    auto obj = Build(object);
    ASSERT_NE(obj, nullptr);

    std::vector<NS::Obj::BoxColliderComponent*> boxes;
    for (NS::Obj::Component* comp : obj->Components())
        if (auto* box = NS::Obj::ComponentCast<NS::Obj::BoxColliderComponent>(comp))
            boxes.push_back(box);
    ASSERT_EQ(boxes.size(), 2u);
    EXPECT_FLOAT_EQ(boxes[0]->HalfExtents().x, 1.0f);
    EXPECT_FLOAT_EQ(boxes[1]->HalfExtents().x, 2.0f);
}

// 実体をリフレクションで書き出し、既定の component へ適用して書き出し直した物が元と一致する
// 保存を data でなく実体から作る前提。registry で組める component だけを対象にする
TEST_F(ObjectBuildTest, LiveComponentsSerializeRoundTripFaithfully)
{
    ObjectData object = MakeFreeObject(BoxColliderData(Vector3{1.0f, 2.0f, 3.0f}));
    NS::Obj::SetObjectPosition(object, Vector3{3.0f, 4.0f, 5.0f});
    NS::Obj::SetObjectScale(object, Vector3{2.0f, 1.0f, 0.5f});

    auto obj = Build(object);
    ASSERT_NE(obj, nullptr);

    const nlohmann::json components = NS::Obj::SerializeGameObjectComponents(*obj);
    ASSERT_EQ(components.size(), obj->Components().size());

    // 各 component を型名から既定生成し、書き出した fields を書き戻して、書き出し直した物が元に戻るか見る
    NS::Obj::GameObject rebuilt;
    for (const auto& compJson : components)
    {
        const std::string typeName = compJson.at("type").get<std::string>();
        // transform は登録一覧に無く GameObject が最初から 1 つ持っているので、生成せずそれへ書き戻す
        NS::Obj::Component* fresh = nullptr;
        if (typeName == NS::Obj::k_TransformTypeName)
            fresh = rebuilt.FindComponent<NS::Obj::TransformComponent>();
        else
            fresh = NS::Obj::CreateComponent(typeName, rebuilt);
        ASSERT_NE(fresh, nullptr) << typeName;
        NS::Obj::ApplyJsonFields(*fresh, compJson.at("fields"));
        EXPECT_EQ(NS::Obj::SerializeComponent(*fresh), compJson) << typeName;
    }
}

// 実体を CaptureObjectData で値データへ忠実に写し、素の GameObject へ ApplyObjectComponents で復元すると
// 元 live と component JSON が一致する。undo とプレイ↔編集の退避・復元に使う機構を直接確かめる
TEST_F(ObjectBuildTest, CaptureObjectDataRestoresFaithfully)
{
    ObjectData object = MakeFreeObject(BoxColliderData(Vector3{1.0f, 2.0f, 3.0f}));
    NS::Obj::SetObjectPosition(object, Vector3{3.0f, 4.0f, 5.0f});
    NS::Obj::SetObjectScale(object, Vector3{2.0f, 1.0f, 0.5f});

    auto live = Build(object);
    ASSERT_NE(live, nullptr);

    const ObjectData snapshot = NS::Obj::CaptureObjectData(*live);
    ASSERT_EQ(snapshot.components.size(), live->Components().size());

    NS::Obj::GameObject restored;
    NS::Obj::ApplyObjectComponents(restored, snapshot, {});
    EXPECT_EQ(NS::Obj::SerializeGameObjectComponents(restored), NS::Obj::SerializeGameObjectComponents(*live));
}

// データの active は保存の往復で残り、読み直した実体にも false のまま乗る
TEST_F(ObjectBuildTest, DisabledComponentSurvivesRoundTrip)
{
    ObjectData object = MakeFreeObject(BoxColliderData(Vector3{0.5f, 0.5f, 0.5f}));
    NS::Obj::SetComponentEntryEnabled(object.components[0], false);

    auto live = Build(object);
    ASSERT_NE(live, nullptr);
    auto* mesh = live->FindComponent<NS::Obj::MeshRendererComponent>();
    ASSERT_NE(mesh, nullptr);
    EXPECT_FALSE(mesh->IsEnabled());

    const ObjectData snapshot = NS::Obj::CaptureObjectData(*live);
    const nlohmann::json* entry = NS::Obj::FindComponentEntry(snapshot, "MeshRendererComponent");
    ASSERT_NE(entry, nullptr);
    EXPECT_FALSE(NS::Obj::ComponentEntryEnabled(*entry));
}

// モード切替で休止させただけの component は保存に持ち込まない。書き込むと読み直しでも動かなくなる
TEST_F(ObjectBuildTest, SleepingComponentIsSavedAsEnabled)
{
    ObjectData object = MakeFreeObject(BoxColliderData(Vector3{0.5f, 0.5f, 0.5f}));

    auto live = Build(object);
    ASSERT_NE(live, nullptr);
    auto* mesh = live->FindComponent<NS::Obj::MeshRendererComponent>();
    ASSERT_NE(mesh, nullptr);

    mesh->SetActive(false);

    const ObjectData snapshot = NS::Obj::CaptureObjectData(*live);
    const nlohmann::json* entry = NS::Obj::FindComponentEntry(snapshot, "MeshRendererComponent");
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(NS::Obj::ComponentEntryEnabled(*entry));
}
