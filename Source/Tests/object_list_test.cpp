#include "Editor/EditorObjects.h"
#include "Game/Level/FollowCameraObject.h"
#include "Game/Player.h"
#include "Runtime/Core/Sphere.h"
#include "Runtime/Object/Components/ThirdPersonFollow.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"

#include <Runtime/Object/AssetManager.h>
#include <Runtime/Object/Components/BoxCollider.h>
#include <Runtime/Object/Components/MeshCollider.h>
#include <Runtime/Object/Components/PlacedVirtualCamera.h>
#include <Runtime/Object/Components/ThirdPersonFollow.h>
#include <Runtime/Object/Components/VirtualCamera.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/ObjectList.h>
#include <Runtime/Object/Reflection/ObjectBuilder.h>
#include <Runtime/Object/Reflection/ObjectRef.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Physics/MeshCollision.h>
#include <Runtime/Physics/PhysicsScene.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <gtest/gtest.h>
#include <memory>
#include <utility>
#include <vector>

using NS::Obj::SceneData;
using NS::Obj::ObjectList;

namespace
{
    // ObjectList は型付き控えを持たないので、テストも本番と同じ問い合わせ口から集める
    template <class T> std::vector<T*> Collect(const ObjectList& objects)
    {
        std::vector<T*> result;
        objects.ForEachComponent<T>([&result](T& comp) { result.push_back(&comp); });
        return result;
    }

    // 本番 Scene と同じ組み方: BuildSceneObject の汎用構築を ObjectList へ渡す
    NS::Obj::ObjectFactoryFn MakeFactory(NS::Obj::AssetManager& assets, const SceneData&)
    {
        return [&assets](const NS::Obj::ObjectData& entry) { return NS::Obj::BuildSceneObject(entry, &assets); };
    }

    // 帯の横断更新の検証用。OnUpdate が呼ばれた順を共有の並びへ書き足す
    class BandRecordingComponent : public NS::Obj::Component
    {
    public:
        BandRecordingComponent(int priority, std::vector<int>* order, int id) noexcept
            : Component(priority), m_order(order), m_id(id)
        {}
        void OnUpdate() override { m_order->push_back(m_id); }

    private:
        std::vector<int>* m_order = nullptr;
        int m_id = 0;
    };

    // 本番の更新経路の検証用。OnUpdate が呼ばれた回数を数える
    class CountingComponent : public NS::Obj::Component
    {
    public:
        void OnUpdate() override { ++m_count; }
        [[nodiscard]] int Count() const noexcept { return m_count; }

    private:
        int m_count = 0;
    };

    // collider は持ち主の Scene からしか PhysicsScene を受け取らないので、Spawn した配置物も Scene へ結ぶ
    // ObjectList 自身は Scene を知らない。Rebuild だけが引数で受けて結ぶ
    struct PhysicsStage
    {
        NS::Obj::Scene scene;
        ObjectList objects;
        NS::Phys::PhysicsScene& physics = scene.Physics();

        NS::Obj::GameObject* Spawn()
        {
            NS::Obj::GameObject* obj = objects.Spawn<NS::Obj::GameObject>();
            obj->AttachScene(&scene);
            return obj;
        }
    };
} // namespace

TEST(ObjectListTest, InitialStateIsEmpty)
{
    ObjectList objects;
    EXPECT_EQ(objects.ObjectCount(), 0u);
}

TEST(ObjectListTest, BuildFollowsWrittenSequence)
{
    SceneData level;
    level.objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    level.objects.push_back(NS::Editor::MakeCellObject(1, 0, 0));
    NS::Obj::EnsureUniqueObjectIds(level);
    const std::uint32_t first = level.objects[0].objectId;
    const std::uint32_t second = level.objects[1].objectId;

    NS::Obj::Scene scene;
    NS::Obj::AssetManager assets{std::string{"."}};
    ObjectList objects;
    objects.Rebuild(level, scene, MakeFactory(assets, level));

    ASSERT_EQ(objects.ObjectCount(), std::size_t{2});
    EXPECT_EQ(objects.ObjectAt(0)->Id(), first);
    EXPECT_EQ(objects.ObjectAt(1)->Id(), second);
}

TEST(ObjectListTest, InactiveObjectHasNoCollision)
{
    SceneData level;
    level.objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    NS::Obj::EnsureUniqueObjectIds(level);
    level.objects[0].active = false;

    NS::Obj::Scene scene;
    NS::Obj::AssetManager assets{std::string{"."}};
    ObjectList objects;
    objects.Rebuild(level, scene, MakeFactory(assets, level));

    ASSERT_EQ(objects.ObjectCount(), std::size_t{1});
    EXPECT_FALSE(objects.ObjectAt(0)->IsActiveSelf());
    // active を切った配置物はすり抜ける
    NS::Phys::PhysicsScene& physics = scene.Physics();
    objects.SyncPhysics(physics);
    EXPECT_EQ(physics.BodyCount(), 0u);
}

TEST(ObjectListTest, TriggerBoxHasNoSolidCollision)
{
    SceneData level;
    NS::Obj::ObjectData object{};
    nlohmann::json box = NS::Obj::MakeComponentEntry("BoxCollider");
    NS::Obj::SetField(box, "トリガー", true);
    object.components = nlohmann::json::array({std::move(box)});
    level.objects.push_back(std::move(object));
    NS::Obj::EnsureUniqueObjectIds(level);

    NS::Obj::Scene scene;
    NS::Obj::AssetManager assets{std::string{"."}};
    ObjectList objects;
    objects.Rebuild(level, scene, MakeFactory(assets, level));

    ASSERT_EQ(objects.ObjectCount(), std::size_t{1});
    // トリガの箱は sensor body として登録されるが、固形の衝突応答は起こさない
    NS::Phys::PhysicsScene& physics = scene.Physics();
    objects.SyncPhysics(physics);
    EXPECT_EQ(physics.BodyCount(), 1u);
}

// 古い当たりが Rebuild 後に残ると、編集で消した block へ当たり続ける
TEST(ObjectListTest, RebuildDropsTheCollidersOfTheObjectsItReplaces)
{
    SceneData level;
    level.objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    NS::Obj::EnsureUniqueObjectIds(level);

    NS::Obj::Scene scene;
    NS::Obj::AssetManager assets{std::string{"."}};
    ObjectList objects;
    objects.Rebuild(level, scene, MakeFactory(assets, level));

    NS::Phys::PhysicsScene& physics = scene.Physics();
    objects.SyncPhysics(physics);
    ASSERT_EQ(physics.BodyCount(), 1u);

    objects.Rebuild(SceneData{}, scene, nullptr);

    EXPECT_EQ(physics.BodyCount(), 0u);
    EXPECT_EQ(objects.ObjectCount(), 0u);
}

TEST(ObjectListTest, ClearEmptiesEverything)
{
    ObjectList objects;
    objects.Clear();
    EXPECT_EQ(objects.ObjectCount(), 0u);
}

// プレイヤー実体も他の配置物と同じ一本道で組まれ、データが決めた id の解決で実体が引ける
TEST(ObjectListTest, RebuildBuildsPlayerAndResolvesItById)
{
    SceneData level;
    level.objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    level.objects.push_back(MakePlayerObject(NS::Core::Vector3{0.0f, 1.41f, 0.0f}, NS::Core::Quaternion{}));
    NS::Obj::EnsureUniqueObjectIds(level);

    NS::Obj::Scene scene;
    NS::Obj::AssetManager assets{std::string{"."}};
    ObjectList objects;
    objects.Rebuild(level, scene, MakeFactory(assets, level));

    // grid block とプレイヤーの両方が組まれ、id 解決は所有リスト内の実体を指す
    ASSERT_EQ(objects.ObjectCount(), 2u);
    const std::size_t playerIndex = FindPlayerObjectIndex(level);
    ASSERT_NE(playerIndex, NS::Obj::k_NoObjectIndex);
    NS::Obj::GameObject* resolved = objects.FindObject(NS::Obj::ObjectRef{level.objects[playerIndex].objectId});
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved, objects.ObjectAt(1));
    EXPECT_FLOAT_EQ(resolved->Root().Position().y, 1.41f);
}

// 追従カメラの配置物は Rebuild 後も休止のまま問い合わせで引け、Target 参照が実体へ解決される
// 参照先より前に並ぶ前方参照でも、組み立てを先に済ませてから開始する二段組みで解決できる
TEST(ObjectListTest, RebuildBakesFollowCameraAndResolvesTarget)
{
    SceneData level;
    level.objects.push_back(NS::Game::Level::MakeFollowCameraObject(0u));
    level.objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    NS::Obj::EnsureUniqueObjectIds(level);
    // 追従先は自分より後ろに並ぶ grid block。追従対象の参照を採番後の実 id へ差し替える
    for (nlohmann::json& component : level.objects[0].components)
        if (NS::Obj::HasField(component, "追従対象"))
            NS::Obj::SetField(component, "追従対象", NS::Obj::ObjectRef{level.objects[1].objectId});

    NS::Obj::Scene scene;
    NS::Obj::AssetManager assets{std::string{"."}};
    // 参照の解決は scene の ObjectList を引くので、組むのは scene 自身の ObjectList
    NS::Obj::ObjectList& objects = scene.Objects();
    objects.Rebuild(level, scene, MakeFactory(assets, level));

    const auto follows = Collect<NS::Obj::ThirdPersonFollow>(objects);
    ASSERT_EQ(follows.size(), 1u);
    auto* follow = follows[0];
    EXPECT_FALSE(follow->IsActive());
    // CameraBrain の登録が使う抽象基底の問い合わせでも同じ実体が引ける
    const auto vcams = Collect<NS::Obj::VirtualCamera>(objects);
    ASSERT_EQ(vcams.size(), 1u);
    EXPECT_EQ(vcams[0], follow);
    // far plane 100 はコンストラクタの既定。データが持つのは追従対象だけ
    EXPECT_FLOAT_EQ(follow->FarPlane(), 100.0f);

    // 追従先の解決: ObjectList が組んだ grid block の Root を指す
    // データの分 + シーンに常駐するカメラ 1 体。一時オブジェクトは末尾へ回るので添字は動かない
    ASSERT_EQ(objects.ObjectCount(), 3u);
    EXPECT_EQ(follow->Target(), &objects.ObjectAt(1)->Root());
}

// component にも永続 id が振られ、object と同じ番号空間で誰とも重ならない
TEST(ObjectListTest, EnsureUniqueObjectIdsNumbersComponents)
{
    SceneData level;
    level.objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    level.objects.push_back(NS::Editor::MakeCellObject(1, 0, 0));

    NS::Obj::EnsureUniqueObjectIds(level);

    std::vector<std::uint32_t> ids;
    for (const NS::Obj::ObjectData& object : level.objects)
    {
        ids.push_back(object.objectId);
        for (const nlohmann::json& entry : object.components)
            ids.push_back(NS::Obj::ComponentEntryId(entry));
    }

    // 0 (未採番) が残っていない
    for (const std::uint32_t id : ids)
        EXPECT_NE(id, 0u);
    // object と component をまたいで重複が無い
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(std::adjacent_find(ids.begin(), ids.end()), ids.end());
}

// データの id が実体へ書き込まれ、保存で往復しても同じ番号のまま
TEST(ObjectListTest, ComponentIdSurvivesBuildAndCapture)
{
    SceneData level;
    level.objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    NS::Obj::EnsureUniqueObjectIds(level);

    NS::Obj::Scene scene;
    NS::Obj::AssetManager assets{std::string{"."}};
    ObjectList objects;
    objects.Rebuild(level, scene, MakeFactory(assets, level));

    ASSERT_EQ(objects.ObjectCount(), 1u);
    NS::Obj::GameObject* live = objects.ObjectAt(0);

    // データに書かれた id がそのまま実体に載っている
    const std::uint32_t dataId = NS::Obj::ComponentEntryId(level.objects[0].components[0]);
    ASSERT_NE(dataId, 0u);
    const std::string_view dataType = NS::Obj::ComponentEntryType(level.objects[0].components[0]);
    NS::Obj::Component* matched = nullptr;
    for (NS::Obj::Component* comp : live->Components())
    {
        if (comp != nullptr && comp->Id() == dataId)
            matched = comp;
    }
    ASSERT_NE(matched, nullptr);
    EXPECT_EQ(matched->GetReflection()->typeName, dataType);

    // 実体から書き戻しても番号は変わらない。落ちると保存のたびに振り直しになる
    const NS::Obj::ObjectData captured = NS::Obj::CaptureObjectData(*live);
    bool found = false;
    for (const nlohmann::json& entry : captured.components)
    {
        if (NS::Obj::ComponentEntryId(entry) == dataId)
            found = true;
    }
    EXPECT_TRUE(found);
}

// 組み直さずに 1 体消しても、破棄済みの実体が id 解決で引けない
TEST(ObjectListTest, RemoveByObjectIdDropsIdResolution)
{
    SceneData level;
    level.objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    level.objects.push_back(NS::Editor::MakeCellObject(1, 0, 0));
    NS::Obj::EnsureUniqueObjectIds(level);
    const std::uint32_t victimId = level.objects[1].objectId;

    NS::Obj::Scene scene;
    NS::Obj::AssetManager assets{std::string{"."}};
    ObjectList objects;
    objects.Rebuild(level, scene, MakeFactory(assets, level));

    ASSERT_NE(objects.FindObject(NS::Obj::ObjectRef{victimId}), nullptr);

    objects.RemoveByObjectId(victimId);

    EXPECT_EQ(objects.ObjectCount(), 1u);
    EXPECT_EQ(objects.FindObject(NS::Obj::ObjectRef{victimId}), nullptr);
}

// 1 体消したら当たり箱もその場で減る。消えた物に当たり続けない
TEST(ObjectListTest, RemoveByObjectIdDropsCollider)
{
    SceneData level;
    level.objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    level.objects.push_back(NS::Editor::MakeCellObject(1, 0, 0));
    NS::Obj::EnsureUniqueObjectIds(level);
    const std::uint32_t victimId = level.objects[1].objectId;

    NS::Obj::Scene scene;
    NS::Obj::AssetManager assets{std::string{"."}};
    ObjectList objects;
    objects.Rebuild(level, scene, MakeFactory(assets, level));

    NS::Phys::PhysicsScene& physics = scene.Physics();
    objects.SyncPhysics(physics);
    ASSERT_EQ(physics.BodyCount(), 2u);

    objects.RemoveByObjectId(victimId);

    EXPECT_EQ(physics.BodyCount(), 1u);
}

// 据え置きカメラの配置物は問い合わせで引け、エリア外の非アクティブで組み上がる
TEST(ObjectListTest, RebuildBakesPlacedCamerasInactive)
{
    SceneData level;
    NS::Obj::ObjectData cameraObject{};
    NS::Obj::SetObjectPosition(cameraObject, NS::Core::Vector3{8.0f, 0.0f, 0.0f});
    nlohmann::json comp = NS::Obj::MakeComponentEntry("PlacedVirtualCamera");
    NS::Obj::SetField(comp, "優先度", 20);
    cameraObject.components.push_back(std::move(comp));
    level.objects.push_back(std::move(cameraObject));

    NS::Obj::Scene scene;
    NS::Obj::AssetManager assets{std::string{"."}};
    ObjectList objects;
    objects.Rebuild(level, scene, MakeFactory(assets, level));

    const auto placedCameras = Collect<NS::Obj::PlacedVirtualCamera>(objects);
    ASSERT_EQ(placedCameras.size(), 1u);
    auto* placed = placedCameras[0];
    EXPECT_FALSE(placed->IsActive());
    EXPECT_EQ(placed->VcamPriority(), 20);
    // 視点位置は object の Transform から来る
    EXPECT_FLOAT_EQ(placed->ViewPosition().x, 8.0f);
}

// 指定した帯だけが回る。帯をどの順で回すかは呼ぶ側の並びで決まる
TEST(ObjectListTest, UpdateObjectsRunsOnlyRequestedBand)
{
    ObjectList objects;
    std::vector<int> order;
    auto* mover = objects.Spawn<NS::Obj::GameObject>();
    mover->AddComponent<BandRecordingComponent>(NS::Obj::TickPriority::EarlyUpdate, &order, 1);
    mover->AddComponent<BandRecordingComponent>(NS::Obj::TickPriority::Update, &order, 2);
    auto* rules = objects.Spawn<NS::Obj::GameObject>();
    rules->AddComponent<BandRecordingComponent>(NS::Obj::TickPriority::Update + 100, &order, 3);
    rules->AddComponent<BandRecordingComponent>(NS::Obj::TickPriority::LateUpdate, &order, 4);
    auto* camera = objects.Spawn<NS::Obj::GameObject>();
    camera->AddComponent<BandRecordingComponent>(NS::Obj::TickPriority::LateUpdate + 50, &order, 5);

    // EarlyUpdate はすぐ上の Update を巻き込まない
    objects.UpdateObjects(NS::Obj::TickPriority::EarlyUpdate, NS::Obj::TickPriority::Update);
    EXPECT_EQ(order, (std::vector<int>{1}));

    // Update は帯の途中 (+100) まで含み、すぐ上の LateUpdate を巻き込まない
    objects.UpdateObjects(NS::Obj::TickPriority::Update, NS::Obj::TickPriority::LateUpdate);
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));

    // LateUpdate は末尾の帯なので +50 の後方まで全部回る
    objects.UpdateObjects(NS::Obj::TickPriority::LateUpdate);
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3, 4, 5}));
}

// 同じ帯の中は配置物の並び順。帯の途中の priority 値もその帯に含まれる
TEST(ObjectListTest, UpdateObjectsRunsSameBandInObjectOrder)
{
    ObjectList objects;
    std::vector<int> order;
    auto first = std::make_unique<NS::Obj::GameObject>();
    first->AddComponent<BandRecordingComponent>(NS::Obj::TickPriority::Update, &order, 1);
    auto second = std::make_unique<NS::Obj::GameObject>();
    second->AddComponent<BandRecordingComponent>(NS::Obj::TickPriority::Update, &order, 2);
    second->AddComponent<BandRecordingComponent>(NS::Obj::TickPriority::Update + 50, &order, 3);
    objects.Append(std::move(first));
    objects.Append(std::move(second));

    objects.UpdateObjects(NS::Obj::TickPriority::Update);
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

// 一時オブジェクトも配置物と同じ帯に乗る。更新経路は 1 本で、違いは保存・凍結に写らない事だけ
TEST(ObjectListTest, BandUpdatesIncludeTransientObjects)
{
    ObjectList objects;
    std::vector<int> order;
    auto transient = std::make_unique<NS::Obj::GameObject>();
    transient->SetTransient(true);
    transient->AddComponent<BandRecordingComponent>(NS::Obj::TickPriority::Update, &order, 1);
    objects.Append(std::move(transient));

    objects.UpdateObjects(NS::Obj::TickPriority::Update);
    EXPECT_EQ(order, (std::vector<int>{1}));
}

// active を切った component は帯の横断更新でも呼ばれない
TEST(ObjectListTest, BandUpdatesSkipInactiveComponents)
{
    ObjectList objects;
    std::vector<int> order;
    auto obj = std::make_unique<NS::Obj::GameObject>();
    auto* sleeping = obj->AddComponent<BandRecordingComponent>(NS::Obj::TickPriority::Update, &order, 1);
    sleeping->SetActive(false);
    objects.Append(std::move(obj));

    objects.UpdateObjects(NS::Obj::TickPriority::Update);
    EXPECT_TRUE(order.empty());
}

// 通常プレイが通る一括更新。active を切った component はここでも回らない
TEST(ObjectListTest, UpdateAllObjectsSkipsInactiveComponent)
{
    ObjectList objects;
    auto* counter = objects.Spawn<NS::Obj::GameObject>()->AddComponent<CountingComponent>();

    counter->SetActive(false);
    objects.UpdateAllObjects();
    EXPECT_EQ(counter->Count(), 0);

    counter->SetActive(true);
    objects.UpdateAllObjects();
    EXPECT_EQ(counter->Count(), 1);
}

// owner の active を切ると配下 component が一括更新から外れ、戻せばまた回る
TEST(ObjectListTest, UpdateAllObjectsFollowsOwnerActiveFlag)
{
    ObjectList objects;
    auto* owner = objects.Spawn<NS::Obj::GameObject>();
    auto* counter = owner->AddComponent<CountingComponent>();

    owner->SetActive(false);
    objects.UpdateAllObjects();
    EXPECT_EQ(counter->Count(), 0);

    owner->SetActive(true);
    objects.UpdateAllObjects();
    EXPECT_EQ(counter->Count(), 1);
}

TEST(ObjectListTest, SyncPhysicsFillsPhysicsScene)
{
    PhysicsStage stage;
    stage.Spawn()->AddComponent<NS::Obj::BoxCollider>();
    stage.Spawn()->AddComponent<NS::Obj::SphereCollider>();

    stage.objects.SyncPhysics(stage.physics);

    EXPECT_EQ(stage.physics.BodyCount(), 2u);
}

TEST(ObjectListTest, SyncPhysicsIntoPhysicsSceneTwiceKeepsTheCount)
{
    PhysicsStage stage;
    stage.Spawn()->AddComponent<NS::Obj::BoxCollider>();

    stage.objects.SyncPhysics(stage.physics);
    stage.objects.SyncPhysics(stage.physics);

    EXPECT_EQ(stage.physics.BodyCount(), 1u);
}

// 同期は body を作り直さず、collider が覚えている id を維持する
// 形の違う 2 つを置くのは SyncBody が派生ごとに別実装だから。1 種類では 1 つの実装しか通らない
TEST(ObjectListTest, SyncPhysicsIntoPhysicsSceneKeepsEveryBodyId)
{
    PhysicsStage stage;
    auto* box = stage.Spawn()->AddComponent<NS::Obj::BoxCollider>();
    auto* sphere = stage.Spawn()->AddComponent<NS::Obj::SphereCollider>();

    stage.objects.SyncPhysics(stage.physics);
    const JPH::BodyID staleBox = box->BodyId();
    const JPH::BodyID staleSphere = sphere->BodyId();

    stage.objects.SyncPhysics(stage.physics);

    EXPECT_EQ(stage.physics.BodyCount(), 2u);
    EXPECT_EQ(box->BodyId(), staleBox);
    EXPECT_EQ(sphere->BodyId(), staleSphere);
}

// 起きている collider を隣に置くのは、body 0 個を期待すると全部断られても緑になるため
TEST(ObjectListTest, InactiveColliderStaysOutOfPhysicsScene)
{
    PhysicsStage stage;
    stage.Spawn()->AddComponent<NS::Obj::BoxCollider>();
    auto* collider = stage.Spawn()->AddComponent<NS::Obj::BoxCollider>();
    collider->SetActive(false);

    stage.objects.SyncPhysics(stage.physics);

    EXPECT_EQ(stage.physics.BodyCount(), 1u);
    EXPECT_TRUE(collider->BodyId().IsInvalid());
}

// 取り込み形状の三角形も物理へ入る。同期側が形状を名指ししない事の裏取り
TEST(ObjectListTest, MeshColliderTrianglesReachPhysics)
{
    // 法線が上を向く床の三角形。斜辺を x+z=4 まで押し出し、原点を縁でなく内側に置く
    NS::Phys::MeshCollision collision{{NS::Phys::Triangle{NS::Core::Vector3{-4.0f, 0.0f, -4.0f},
                                                          NS::Core::Vector3{-4.0f, 0.0f, 8.0f},
                                                          NS::Core::Vector3{8.0f, 0.0f, -4.0f}}},
                                      nullptr};
    collision.shape = NS::Phys::CreateMeshShape(collision.triangles);

    PhysicsStage stage;
    stage.Spawn()->AddComponent<NS::Obj::MeshCollider>()->SetCollision(&collision);

    stage.objects.SyncPhysics(stage.physics);
    ASSERT_EQ(stage.physics.BodyCount(), 1u);

    float distance = 0.0f;
    EXPECT_TRUE(stage.physics.Raycast(
        NS::Core::Vector3{0.0f, 2.0f, 0.0f}, NS::Core::Vector3{0.0f, -1.0f, 0.0f}, 8.0f, distance));
    EXPECT_NEAR(distance, 2.0f, 1.0e-3f);
}

// 帯を回すたびの確保と並べ替えが 1 フレームの予算をどれだけ食うかを測る
// 時間で合否を決めると環境差で揺れるので、数字を出すだけにして判断は人が行う
TEST(ObjectListTest, UpdateObjectsCostMeasurement)
{
    const auto measure = [](std::size_t objectCount, std::size_t componentsPerObject) {
        ObjectList objects;
        for (std::size_t i = 0; i < objectCount; ++i)
        {
            NS::Obj::GameObject* obj = objects.Spawn<NS::Obj::GameObject>();
            for (std::size_t c = 0; c < componentsPerObject; ++c)
                obj->AddComponent<CountingComponent>();
        }

        constexpr int k_Iterations = 1000;
        const auto start = std::chrono::steady_clock::now();
        for (int n = 0; n < k_Iterations; ++n)
            objects.UpdateObjects(NS::Obj::TickPriority::Update);
        const auto elapsed = std::chrono::steady_clock::now() - start;

        const double perCallMicros =
            std::chrono::duration<double, std::micro>(elapsed).count() / static_cast<double>(k_Iterations);
        // 固定ステップは 1/60 秒
        constexpr double k_FrameBudgetMicros = 1000000.0 / 60.0;
        std::printf("  配置物 %zu 体 x component %zu 個 = %zu 個: %.2f us/回 (1 フレーム予算の %.3f %%)\n",
                    objectCount,
                    componentsPerObject,
                    objectCount * componentsPerObject,
                    perCallMicros,
                    perCallMicros / k_FrameBudgetMicros * 100.0);
    };

    measure(20, 5);
    measure(1000, 4);
}

// 飛んでいる岩の body は collider の持ち物ではない。同期が巻き添えで消すと飛行が途中で止まる
TEST(ObjectListTest, SyncPhysicsKeepsBodiesItDidNotCreate)
{
    PhysicsStage stage;
    stage.Spawn()->AddComponent<NS::Obj::BoxCollider>();

    stage.objects.SyncPhysics(stage.physics);

    NS::Core::Sphere loose;
    loose.center = NS::Core::Vector3{20.0f, 20.0f, 20.0f};
    loose.radius = 0.5f;
    const JPH::BodyID outsider = stage.physics.AddSphere(loose, NS::Phys::ObjectLayers::Rock);

    stage.objects.SyncPhysics(stage.physics);

    EXPECT_EQ(stage.physics.BodyCount(), 2u);
    EXPECT_NEAR(stage.physics.BodyPosition(outsider).y, 20.0f, 1.0e-4f);
}

// 寝かせた collider の body は同期で外れる。残ると壊した物の当たりが固形のまま居座る
TEST(ObjectListTest, SyncPhysicsDropsTheBodyOfADeactivatedCollider)
{
    PhysicsStage stage;
    auto* box = stage.Spawn()->AddComponent<NS::Obj::BoxCollider>();

    stage.objects.SyncPhysics(stage.physics);
    ASSERT_EQ(stage.physics.BodyCount(), 1u);

    box->SetActive(false);
    stage.objects.SyncPhysics(stage.physics);

    EXPECT_EQ(stage.physics.BodyCount(), 0u);
    EXPECT_TRUE(box->BodyId().IsInvalid());
}

// 配置物ごと消える時は破棄の前に OnEndPlay が通る。ここで外さないと body が誰の持ち物でもなくなる
TEST(ObjectListTest, ClearTakesEveryColliderBodyOutOfThePhysicsScene)
{
    NS::Obj::Scene scene;
    auto box = std::make_unique<NS::Obj::GameObject>();
    box->AddComponent<NS::Obj::BoxCollider>();
    scene.SpawnTransient(std::move(box));
    auto sphere = std::make_unique<NS::Obj::GameObject>();
    sphere->AddComponent<NS::Obj::SphereCollider>();
    scene.SpawnTransient(std::move(sphere));

    scene.SyncPhysics();
    ASSERT_EQ(scene.Physics().BodyCount(), 2u);

    scene.Objects().Clear();

    EXPECT_EQ(scene.Physics().BodyCount(), 0u);
}
