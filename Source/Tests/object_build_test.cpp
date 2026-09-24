#include "Game/Player.h"
#include "tuning_field_access.h"

#include <Editor/EditorObjects.h>
#include <Game/Level/Goal.h>
#include <Game/Player/PlayerComponent.h>
#include <Runtime/Core/AABB.h>
#include <Runtime/Core/Sphere.h>
#include <Runtime/Object/AssetManager.h>
#include <Runtime/Object/Components/BoxCollider.h>
#include <Runtime/Object/Components/CapsuleCollider.h>
#include <Runtime/Object/Components/MeshRenderer.h>
#include <Runtime/Object/Components/SlopeCollider.h>
#include <Runtime/Object/Components/SphereCollider.h>
#include <Runtime/Object/Components/TransformComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/ObjectJson.h>
#include <Runtime/Object/ObjectList.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Reflection/ObjectBuilder.h>
#include <Runtime/Object/Reflection/ObjectRef.h>
#include <Runtime/Object/Reflection/ReflectionJson.h>
#include <Runtime/Object/Reflection/TypeRegistry.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Scene/SceneJson.h>
#include <cstdint>
#include <gtest/gtest.h>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using NS::Editor::MakeCellCubeComponents;
    using NS::Editor::MakeCellObject;
    using NS::Core::Vector3;

    // RegisterBuiltins も RegisterSharedMaterials も呼ばない AssetManager。組み込みと共有材質は
    // nullptr を返すが、汎用構築は落ちない
    class ObjectBuildTest : public ::testing::Test
    {
    protected:
        NS::Obj::AssetManager m_assets{std::string{"."}};

        std::unique_ptr<NS::Obj::GameObject> Build(const nlohmann::json& object)
        {
            return NS::Obj::ObjectFromJson(object, &m_assets);
        }
    };

    // 格子に置く素の cube
    nlohmann::json MakeGridCube()
    {
        return MakeCellObject(0, 0, 0);
    }

    // 自由配置物の JSON。見た目の cube と渡された collider を component 直書きで積む
    nlohmann::json MakeFreeObject(nlohmann::json collider)
    {
        nlohmann::json object = NS::Obj::MakeObjectJson();
        nlohmann::json mesh = NS::Obj::MakeComponentEntry("MeshRenderer");
        NS::Obj::SetField(mesh, "メッシュ", "cube");
        NS::Obj::ObjectJsonComponents(object).push_back(std::move(mesh));
        NS::Obj::ObjectJsonComponents(object).push_back(std::move(collider));
        return object;
    }

    nlohmann::json BoxColliderData(const Vector3& half)
    {
        nlohmann::json data = NS::Obj::MakeComponentEntry("BoxCollider");
        NS::Obj::SetField(data, "半径", half);
        return data;
    }

    nlohmann::json SphereColliderData(float radius, const Vector3& offset)
    {
        nlohmann::json data = NS::Obj::MakeComponentEntry("SphereCollider");
        NS::Obj::SetField(data, "半径", radius);
        NS::Obj::SetField(data, "中心オフセット", offset);
        return data;
    }

    nlohmann::json CapsuleColliderData(float radius, float halfHeight)
    {
        nlohmann::json data = NS::Obj::MakeComponentEntry("CapsuleCollider");
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
    std::unique_ptr<NS::Obj::GameObject> obj = Build(MakeGridCube());
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(Has<NS::Obj::MeshRenderer>(*obj));
    EXPECT_TRUE(Has<NS::Obj::BoxCollider>(*obj));
    EXPECT_FALSE(Has<NS::Obj::SphereCollider>(*obj));
    EXPECT_FALSE(Has<NS::Obj::CapsuleCollider>(*obj));
}

TEST_F(ObjectBuildTest, DefaultFreeCubeComponentsAreCubeWithBoxCollider)
{
    nlohmann::json object = NS::Obj::MakeObjectJson(MakeCellCubeComponents());

    std::unique_ptr<NS::Obj::GameObject> obj = Build(object);
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(Has<NS::Obj::MeshRenderer>(*obj));
    NS::Obj::BoxCollider* box = obj->FindComponent<NS::Obj::BoxCollider>();
    ASSERT_NE(box, nullptr);
    const Vector3 half = box->HalfExtents();
    EXPECT_FLOAT_EQ(half.x, 0.5f);
    EXPECT_FLOAT_EQ(half.y, 0.5f);
    EXPECT_FLOAT_EQ(half.z, 0.5f);
}

TEST_F(ObjectBuildTest, FreeBoxHasBoxColliderWithSavedHalfExtents)
{
    std::unique_ptr<NS::Obj::GameObject> obj = Build(MakeFreeObject(BoxColliderData(Vector3{1.0f, 2.0f, 3.0f})));
    ASSERT_NE(obj, nullptr);
    NS::Obj::BoxCollider* box = obj->FindComponent<NS::Obj::BoxCollider>();
    ASSERT_NE(box, nullptr);
    const Vector3 half = box->HalfExtents();
    EXPECT_FLOAT_EQ(half.x, 1.0f);
    EXPECT_FLOAT_EQ(half.y, 2.0f);
    EXPECT_FLOAT_EQ(half.z, 3.0f);
    EXPECT_FALSE(Has<NS::Obj::SphereCollider>(*obj));
    EXPECT_FALSE(Has<NS::Obj::CapsuleCollider>(*obj));
}

TEST_F(ObjectBuildTest, FreeSphereHasOnlySphereCollider)
{
    std::unique_ptr<NS::Obj::GameObject> obj = Build(MakeFreeObject(SphereColliderData(0.7f, Vector3{0.0f, 1.0f, 0.0f})));
    ASSERT_NE(obj, nullptr);
    EXPECT_FALSE(Has<NS::Obj::BoxCollider>(*obj));
    NS::Obj::SphereCollider* sphere = obj->FindComponent<NS::Obj::SphereCollider>();
    ASSERT_NE(sphere, nullptr);
    EXPECT_FALSE(Has<NS::Obj::CapsuleCollider>(*obj));

    const NS::Core::Sphere world = sphere->WorldSphere();
    EXPECT_NEAR(world.radius, 0.7f, 1e-4f);
    EXPECT_NEAR(world.center.y, 1.0f, 1e-4f);
}

TEST_F(ObjectBuildTest, SphereColliderWorldAabbEnclosesSphere)
{
    std::unique_ptr<NS::Obj::GameObject> obj = Build(MakeFreeObject(SphereColliderData(0.7f, Vector3{0.0f, 1.0f, 0.0f})));
    ASSERT_NE(obj, nullptr);
    NS::Obj::SphereCollider* sphere = obj->FindComponent<NS::Obj::SphereCollider>();
    ASSERT_NE(sphere, nullptr);
    const NS::Core::AABB aabb = sphere->WorldAABB();
    EXPECT_NEAR(aabb.Center.y, 1.0f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.x, 0.7f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.y, 0.7f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.z, 0.7f, 1e-4f);
}

TEST_F(ObjectBuildTest, FreeCapsuleHasOnlyCapsuleCollider)
{
    std::unique_ptr<NS::Obj::GameObject> obj = Build(MakeFreeObject(CapsuleColliderData(0.4f, 0.9f)));
    ASSERT_NE(obj, nullptr);
    EXPECT_FALSE(Has<NS::Obj::BoxCollider>(*obj));
    NS::Obj::CapsuleCollider* capsule = obj->FindComponent<NS::Obj::CapsuleCollider>();
    ASSERT_NE(capsule, nullptr);
    EXPECT_FALSE(Has<NS::Obj::SphereCollider>(*obj));

    const NS::Phys::Capsule worldCapsule = capsule->WorldCapsule();
    EXPECT_NEAR(worldCapsule.radius, 0.4f, 1e-4f);
    EXPECT_NEAR(worldCapsule.halfHeight, 0.9f, 1e-4f);
}

TEST_F(ObjectBuildTest, CapsuleColliderWorldAabbEnclosesCapsule)
{
    std::unique_ptr<NS::Obj::GameObject> obj = Build(MakeFreeObject(CapsuleColliderData(0.4f, 0.9f)));
    ASSERT_NE(obj, nullptr);
    NS::Obj::CapsuleCollider* capsule = obj->FindComponent<NS::Obj::CapsuleCollider>();
    ASSERT_NE(capsule, nullptr);
    const NS::Core::AABB aabb = capsule->WorldAABB();
    EXPECT_NEAR(aabb.Extents.x, 0.4f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.y, 1.3f, 1e-4f);
    EXPECT_NEAR(aabb.Extents.z, 0.4f, 1e-4f);
}

TEST_F(ObjectBuildTest, TransformAppliedToRoot)
{
    nlohmann::json object = MakeGridCube();
    NS::Obj::SetObjectPosition(object, Vector3{3.0f, 4.0f, 5.0f});

    std::unique_ptr<NS::Obj::GameObject> obj = Build(object);
    ASSERT_NE(obj, nullptr);
    const Vector3 pos = obj->Root().Position();
    EXPECT_FLOAT_EQ(pos.x, 3.0f);
    EXPECT_FLOAT_EQ(pos.y, 4.0f);
    EXPECT_FLOAT_EQ(pos.z, 5.0f);
}

TEST_F(ObjectBuildTest, GridCubeWorldAabbMatchesCellHalfExtents)
{
    std::unique_ptr<NS::Obj::GameObject> obj = Build(MakeGridCube());
    ASSERT_NE(obj, nullptr);
    NS::Obj::BoxCollider* box = obj->FindComponent<NS::Obj::BoxCollider>();
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
    nlohmann::json object = MakeFreeObject(BoxColliderData(Vector3{1.0f, 2.0f, 3.0f}));
    NS::Obj::SetObjectPosition(object, Vector3{2.0f, 0.0f, 0.0f});

    std::unique_ptr<NS::Obj::GameObject> obj = Build(object);
    ASSERT_NE(obj, nullptr);
    NS::Obj::BoxCollider* box = obj->FindComponent<NS::Obj::BoxCollider>();
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
    nlohmann::json object = NS::Obj::MakeObjectJson();
    NS::Obj::ObjectJsonComponents(object).push_back(BoxColliderData(Vector3{1.0f, 2.0f, 3.0f}));

    std::unique_ptr<NS::Obj::GameObject> obj = Build(object);
    ASSERT_NE(obj, nullptr);
    NS::Obj::BoxCollider* boxComp = obj->FindComponent<NS::Obj::BoxCollider>();
    ASSERT_NE(boxComp, nullptr);
    const Vector3 half = boxComp->HalfExtents();
    EXPECT_FLOAT_EQ(half.x, 1.0f);
    EXPECT_FLOAT_EQ(half.y, 2.0f);
    EXPECT_FLOAT_EQ(half.z, 3.0f);
}

// components を持たない object は配置物として組まれず nullptr が返る
TEST_F(ObjectBuildTest, EmptyComponentsBuildsNothing)
{
    const nlohmann::json object = NS::Obj::MakeObjectJson();
    ASSERT_TRUE(NS::Obj::ObjectJsonComponents(object).empty());

    EXPECT_EQ(Build(object), nullptr);
}

// 材質の参照が .. で ContentRoot の外へ出るなら拒否する。落ちずに component が揃うことだけを見る
TEST_F(ObjectBuildTest, AssetPathTraversalRejectedFallsBackToDefault)
{
    nlohmann::json object = MakeFreeObject(BoxColliderData(Vector3{0.5f, 0.5f, 0.5f}));
    NS::Obj::SetField(NS::Obj::ObjectJsonComponents(object)[0], "マテリアル", "../evil.mat");

    // const char* が string_view 版へ解決され、bool でなく文字列として書かれていること
    ASSERT_TRUE(NS::Obj::ObjectJsonComponents(object)[0]["fields"]["マテリアル"].is_string());

    std::unique_ptr<NS::Obj::GameObject> obj = Build(object);
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(Has<NS::Obj::MeshRenderer>(*obj));
    EXPECT_TRUE(Has<NS::Obj::BoxCollider>(*obj));
}

// 45 度スロープの雛形は表示名が Slope 45 で、45 度の SlopeCollider を持ち、回せる
TEST_F(ObjectBuildTest, GridSlopeHasSlopeColliderAndDisplaysAsSlope45)
{
    nlohmann::json slope = MakeCellObject(0, 0, 0);
    NS::Obj::ObjectJsonComponents(slope) = NS::Editor::MakeCellSlopeComponents(45.0f);

    EXPECT_STREQ(NS::Editor::ObjectDisplayName(slope), "Slope 45");
    EXPECT_TRUE(NS::Editor::IsRotatableObject(slope));

    std::unique_ptr<NS::Obj::GameObject> obj = Build(slope);
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(Has<NS::Obj::MeshRenderer>(*obj));
    NS::Obj::SlopeCollider* collider = obj->FindComponent<NS::Obj::SlopeCollider>();
    ASSERT_NE(collider, nullptr);
    EXPECT_FLOAT_EQ(collider->AngleDegrees(), 45.0f);
    EXPECT_FALSE(Has<NS::Obj::BoxCollider>(*obj));
}

// ゴールの雛形は接触クリアの印を持ち、表示名は Goal。BoxCollider も SlopeCollider も積まないので回せない
TEST_F(ObjectBuildTest, GoalHasMarkerAndDisplaysAsGoal)
{
    nlohmann::json goal = MakeCellObject(0, 0, 0);
    NS::Obj::ObjectJsonComponents(goal) = NS::Editor::MakeGoalComponents();

    EXPECT_STREQ(NS::Editor::ObjectDisplayName(goal), "Goal");
    EXPECT_FALSE(NS::Editor::IsRotatableObject(goal));

    std::unique_ptr<NS::Obj::GameObject> obj = Build(goal);
    ASSERT_NE(obj, nullptr);
    EXPECT_NE(obj->FindComponent<NS::Game::Level::Goal>(), nullptr);
}

// 格子に置く cube は固形なので回せる
TEST_F(ObjectBuildTest, GridCubeIsRotatable)
{
    EXPECT_TRUE(NS::Editor::IsRotatableObject(MakeCellObject(0, 0, 0)));
}

// 自由配置の cube も固形 box なので回せる。固形判定には BoxCollider が要るので、空構成の marker は回せない
TEST_F(ObjectBuildTest, FreeCubeRotatableButEmptyMarkerNot)
{
    const nlohmann::json freeCube = NS::Obj::MakeObjectJson(MakeCellCubeComponents());
    EXPECT_TRUE(NS::Editor::IsRotatableObject(freeCube));

    const nlohmann::json marker = NS::Obj::MakeObjectJson();
    EXPECT_FALSE(NS::Editor::IsRotatableObject(marker));
}

// プレイヤー実体は class から Player 派生の GameObject に二重生成なしで組まれる
TEST_F(ObjectBuildTest, PlayerObjectBuildsPlayerTyped)
{
    const nlohmann::json data = MakePlayerObject(Vector3{1.0f, 2.0f, 3.0f}, NS::Core::Quaternion{});
    EXPECT_EQ(NS::Obj::ObjectJsonClass(data), "Player");

    std::unique_ptr<NS::Obj::GameObject> obj = Build(data);
    ASSERT_NE(obj, nullptr);
    // 実行時型情報は切っているため、クラス名で型選択を確かめてから GameObject の API で見る
    ASSERT_STREQ(obj->ClassName(), "Player");

    // コンストラクタが積む分と同じ数。transform も GameObject が持つので二重にはならない
    EXPECT_EQ(obj->Components().size(), Player{}.Components().size());

    // 姿勢は他の配置物と同じくデータから乗る
    EXPECT_FLOAT_EQ(obj->Root().Position().y, 2.0f);
}

// プレイヤーのデータ構成は Player のコンストラクタから吸い出した型名の一覧。値を持つのは transform だけ
TEST_F(ObjectBuildTest, PlayerObjectJsonIsSparseTypeListFromClass)
{
    const nlohmann::json data = MakePlayerObject(Vector3{}, NS::Core::Quaternion{});

    ASSERT_EQ(NS::Obj::ObjectJsonComponents(data).size(), Player{}.Components().size());
    EXPECT_NE(NS::Obj::FindComponentEntry(data, "MeshRenderer"), nullptr);
    EXPECT_NE(NS::Obj::FindComponentEntry(data, "PlayerComponent"), nullptr);
    EXPECT_NE(NS::Obj::FindComponentEntry(data, "PlayerStateManager"), nullptr);
    EXPECT_NE(NS::Obj::FindComponentEntry(data, "PlayerInput"), nullptr);
    EXPECT_NE(NS::Obj::FindComponentEntry(data, "Health"), nullptr);
    EXPECT_NE(NS::Obj::FindComponentEntry(data, "Shadow"), nullptr);
    for (const nlohmann::json& entry : NS::Obj::ObjectJsonComponents(data))
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
    std::unique_ptr<NS::Obj::GameObject> obj = Build(MakePlayerObject(Vector3{}, NS::Core::Quaternion{}));
    ASSERT_NE(obj, nullptr);
    NS::Obj::MeshRenderer* mesh = obj->FindComponent<NS::Obj::MeshRenderer>();
    ASSERT_NE(mesh, nullptr);

    EXPECT_EQ(mesh->MeshRef(), "cube");
    EXPECT_EQ(mesh->MaterialRef(), "player");

    // 個体色は getter が無いのでリフレクションフィールド越しに読む
    const NS::Obj::ReflectionInfo* info = NS::Obj::MeshRenderer::StaticReflection();
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
    nlohmann::json data = MakePlayerObject(Vector3{}, NS::Core::Quaternion{});
    for (nlohmann::json& entry : NS::Obj::ObjectJsonComponents(data))
        if (NS::Obj::ComponentEntryType(entry) == "PlayerComponent")
            NS::Obj::SetField(entry, "コヨーテ時間", 0.125f);

    std::unique_ptr<NS::Obj::GameObject> obj = Build(data);
    ASSERT_NE(obj, nullptr);
    obj->OnStart();
    NS::Game::Player::PlayerComponent* movement = obj->FindComponent<NS::Game::Player::PlayerComponent>();
    ASSERT_NE(movement, nullptr);
    EXPECT_FLOAT_EQ(NsTest::ReadTuningField(*movement, "コヨーテ時間"), 0.125f);
}

// Player のデータはコンストラクタが積む型名の一覧なので、どの項目も既存の実体に当たり CreateComponent を通らない
// id を書くのは組み立てでなく配置物を積む ObjectList なので、本番と同じ Scene の読込を通して確かめる
// 書き込みが組み立てと別の対応を引くと、コンストラクタが積んだ実体に id が載らないか入れ違う
TEST_F(ObjectBuildTest, PlayerObjectAppliesDataIdsToConstructorComponents)
{
    nlohmann::json data = MakePlayerObject(Vector3{}, NS::Core::Quaternion{});
    const std::uint32_t objectId = 1234u;
    NS::Obj::SetObjectJsonId(data, objectId);

    nlohmann::json* entry = nullptr;
    for (nlohmann::json& candidate : NS::Obj::ObjectJsonComponents(data))
    {
        if (NS::Obj::ComponentEntryType(candidate) == "PlayerComponent")
        {
            entry = &candidate;
        }
    }
    ASSERT_NE(entry, nullptr);

    const std::uint32_t dataId = 4321u;
    NS::Obj::SetComponentEntryId(*entry, dataId);

    nlohmann::json level = NS::Obj::MakeSceneJson();
    NS::Obj::SceneJsonObjects(level).push_back(std::move(data));
    NS::Obj::Scene scene;
    scene.LoadJson(std::move(level));

    NS::Obj::GameObject* obj = scene.Objects().FindObject(NS::Obj::ObjectRef{objectId});
    ASSERT_NE(obj, nullptr);
    NS::Game::Player::PlayerComponent* movement = obj->FindComponent<NS::Game::Player::PlayerComponent>();
    ASSERT_NE(movement, nullptr);
    EXPECT_EQ(movement->Id(), dataId);

    // 実体から書き戻しても同じ番号のまま。落ちると保存のたびに振り直しになる
    const nlohmann::json captured = NS::Obj::ObjectToJson(*obj);
    const nlohmann::json* capturedEntry = NS::Obj::FindComponentEntry(captured, "PlayerComponent");
    ASSERT_NE(capturedEntry, nullptr);
    EXPECT_EQ(NS::Obj::ComponentEntryId(*capturedEntry), dataId);
}

// 同型 component を重ねたデータは live でも同数立ち、2 件目が 1 件目へ上書きされない
// 重ねた当たりは 1 つずつ body になる
TEST_F(ObjectBuildTest, DuplicateColliderDataBuildsCompoundColliders)
{
    nlohmann::json object = MakeFreeObject(BoxColliderData(Vector3{1.0f, 1.0f, 1.0f}));
    NS::Obj::ObjectJsonComponents(object).push_back(BoxColliderData(Vector3{2.0f, 2.0f, 2.0f}));

    std::unique_ptr<NS::Obj::GameObject> obj = Build(object);
    ASSERT_NE(obj, nullptr);

    std::vector<NS::Obj::BoxCollider*> boxes;
    for (NS::Obj::Component* comp : obj->Components())
        if (NS::Obj::BoxCollider* box = NS::Obj::ComponentCast<NS::Obj::BoxCollider>(comp))
            boxes.push_back(box);
    ASSERT_EQ(boxes.size(), 2u);
    EXPECT_FLOAT_EQ(boxes[0]->HalfExtents().x, 1.0f);
    EXPECT_FLOAT_EQ(boxes[1]->HalfExtents().x, 2.0f);
}

// 実体をリフレクションで書き出し、既定の component へ適用して書き出し直した物が元と一致する
// 保存を data でなく実体から作る前提。registry で組める component だけを対象にする
TEST_F(ObjectBuildTest, LiveComponentsSerializeRoundTripFaithfully)
{
    nlohmann::json object = MakeFreeObject(BoxColliderData(Vector3{1.0f, 2.0f, 3.0f}));
    NS::Obj::SetObjectPosition(object, Vector3{3.0f, 4.0f, 5.0f});
    NS::Obj::SetObjectScale(object, Vector3{2.0f, 1.0f, 0.5f});

    std::unique_ptr<NS::Obj::GameObject> obj = Build(object);
    ASSERT_NE(obj, nullptr);

    const nlohmann::json components = NS::Obj::SerializeGameObjectComponents(*obj);
    ASSERT_EQ(components.size(), obj->Components().size());

    // 各 component を型名から既定生成し、書き出した fields を書き戻して、書き出し直した物が元に戻るか見る
    NS::Obj::GameObject rebuilt;
    for (const nlohmann::json& compJson : components)
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

// 実体を ObjectToJson で配置物の JSON へ忠実に写し、素の GameObject へ ApplyObjectComponents で復元すると
// 元 live と component JSON が一致する。undo とプレイ↔編集の退避・復元に使う機構を直接確かめる
TEST_F(ObjectBuildTest, ObjectToJsonRestoresFaithfully)
{
    nlohmann::json object = MakeFreeObject(BoxColliderData(Vector3{1.0f, 2.0f, 3.0f}));
    NS::Obj::SetObjectPosition(object, Vector3{3.0f, 4.0f, 5.0f});
    NS::Obj::SetObjectScale(object, Vector3{2.0f, 1.0f, 0.5f});

    std::unique_ptr<NS::Obj::GameObject> live = Build(object);
    ASSERT_NE(live, nullptr);

    const nlohmann::json snapshot = NS::Obj::ObjectToJson(*live);
    ASSERT_EQ(NS::Obj::ObjectJsonComponents(snapshot).size(), live->Components().size());

    NS::Obj::GameObject restored;
    NS::Obj::ApplyObjectComponents(restored, snapshot, {});
    EXPECT_EQ(NS::Obj::SerializeGameObjectComponents(restored), NS::Obj::SerializeGameObjectComponents(*live));
}

// データの active は保存の往復で残り、読み直した実体にも false のまま乗る
TEST_F(ObjectBuildTest, DisabledComponentSurvivesRoundTrip)
{
    nlohmann::json object = MakeFreeObject(BoxColliderData(Vector3{0.5f, 0.5f, 0.5f}));
    NS::Obj::SetComponentEntryEnabled(NS::Obj::ObjectJsonComponents(object)[0], false);

    std::unique_ptr<NS::Obj::GameObject> live = Build(object);
    ASSERT_NE(live, nullptr);
    NS::Obj::MeshRenderer* mesh = live->FindComponent<NS::Obj::MeshRenderer>();
    ASSERT_NE(mesh, nullptr);
    EXPECT_FALSE(mesh->IsEnabled());

    const nlohmann::json snapshot = NS::Obj::ObjectToJson(*live);
    const nlohmann::json* entry = NS::Obj::FindComponentEntry(snapshot, "MeshRenderer");
    ASSERT_NE(entry, nullptr);
    EXPECT_FALSE(NS::Obj::ComponentEntryEnabled(*entry));
}

// モード切替で休止させただけの component は保存に持ち込まない。書き込むと読み直しでも動かなくなる
TEST_F(ObjectBuildTest, SleepingComponentIsSavedAsEnabled)
{
    nlohmann::json object = MakeFreeObject(BoxColliderData(Vector3{0.5f, 0.5f, 0.5f}));

    std::unique_ptr<NS::Obj::GameObject> live = Build(object);
    ASSERT_NE(live, nullptr);
    NS::Obj::MeshRenderer* mesh = live->FindComponent<NS::Obj::MeshRenderer>();
    ASSERT_NE(mesh, nullptr);

    mesh->SetActive(false);

    const nlohmann::json snapshot = NS::Obj::ObjectToJson(*live);
    const nlohmann::json* entry = NS::Obj::FindComponentEntry(snapshot, "MeshRenderer");
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(NS::Obj::ComponentEntryEnabled(*entry));
}
