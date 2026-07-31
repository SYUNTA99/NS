#include "Game/Level/BlockObject.h"
#include "Game/Level/FollowCameraObject.h"
#include "Game/Player.h"
#include "Runtime/Object/Components/ThirdPersonFollowComponent.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <Runtime/Object/AssetManager.h>
#include <Runtime/Object/Components/BoxColliderComponent.h>
#include <Runtime/Object/Components/MeshColliderComponent.h>
#include <Runtime/Object/Components/PlacedVirtualCamera.h>
#include <Runtime/Object/Components/ThirdPersonFollowComponent.h>
#include <Runtime/Object/Components/VirtualCameraComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/ObjectBuilder.h>
#include <Runtime/Object/Reflection/ObjectRef.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/World.h>
#include <Runtime/Physics/PhysicsWorld.h>
#include <utility>
#include <vector>

using NS::Object::SceneData;
using NS::Object::World;

namespace
{
    // world は型付き控えを持たないので、テストも本番の読み手と同じ問い合わせ口から集める
    template <class T> std::vector<T*> Collect(const World& world)
    {
        std::vector<T*> result;
        world.ForEachComponent<T>([&result](T& comp) { result.push_back(&comp); });
        return result;
    }

    // 本番 Scene と同じ組み方: エンジンの汎用構築を World へ渡す
    NS::Object::ObjectFactoryFn MakeFactory(NS::Object::AssetManager& assets, const SceneData&)
    {
        return [&assets](const NS::Object::ObjectData& entry) { return NS::Object::BuildSceneObject(entry, &assets); };
    }

    // 帯の横断更新の検証用。 OnUpdate が呼ばれた順を共有の並びへ書き足す
    class BandRecordingComponent : public NS::Object::Component
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

    // 本番の更新経路の検証用。 OnUpdate が呼ばれた回数を数える
    class CountingComponent : public NS::Object::Component
    {
    public:
        void OnUpdate() override { ++m_count; }
        [[nodiscard]] int Count() const noexcept { return m_count; }

    private:
        int m_count = 0;
    };

} // namespace

TEST(WorldTest, InitialStateIsEmpty)
{
    World world;
    EXPECT_EQ(world.ObjectCount(), 0u);
}

TEST(WorldTest, BuildFollowsObjectOrderNotArrayOrder)
{
    SceneData level;
    level.objects.push_back(NS::Game::Level::MakeCellObject(0, 0, 0));
    level.objects.push_back(NS::Game::Level::MakeCellObject(1, 0, 0));
    level.objects.push_back(NS::Game::Level::MakeCellObject(2, 0, 0));
    NS::Object::EnsureUniqueObjectIds(level);
    const std::uint32_t first = level.objects[0].objectId;
    const std::uint32_t second = level.objects[1].objectId;
    const std::uint32_t third = level.objects[2].objectId;

    // 配列の並びを触らず order だけで前後を入れ替える
    level.objects[0].order = 2;
    level.objects[1].order = 0;
    level.objects[2].order = 1;

    NS::Object::Scene scene;
    scene.CreateSceneSubsystems();
    NS::Physics::PhysicsWorld physics;
    NS::Object::AssetManager assets{std::filesystem::path{"."}};
    World world;
    world.Rebuild(level, scene, physics, MakeFactory(assets, level));

    ASSERT_EQ(world.ObjectCount(), std::size_t{3});
    EXPECT_EQ(world.ObjectAt(0)->Id(), second);
    EXPECT_EQ(world.ObjectAt(1)->Id(), third);
    EXPECT_EQ(world.ObjectAt(2)->Id(), first);
    // 値そのものも実体へ届く。 捕捉が同じ order を書き戻せる
    EXPECT_EQ(world.ObjectAt(0)->Order(), 0u);
    EXPECT_EQ(world.ObjectAt(2)->Order(), 2u);
}

TEST(WorldTest, EqualOrderKeepsWrittenSequence)
{
    // order を持たない古いファイルは書かれた順のまま組み上がる
    SceneData level;
    level.objects.push_back(NS::Game::Level::MakeCellObject(0, 0, 0));
    level.objects.push_back(NS::Game::Level::MakeCellObject(1, 0, 0));
    NS::Object::EnsureUniqueObjectIds(level);
    const std::uint32_t first = level.objects[0].objectId;
    const std::uint32_t second = level.objects[1].objectId;

    NS::Object::Scene scene;
    scene.CreateSceneSubsystems();
    NS::Physics::PhysicsWorld physics;
    NS::Object::AssetManager assets{std::filesystem::path{"."}};
    World world;
    world.Rebuild(level, scene, physics, MakeFactory(assets, level));

    ASSERT_EQ(world.ObjectCount(), std::size_t{2});
    EXPECT_EQ(world.ObjectAt(0)->Id(), first);
    EXPECT_EQ(world.ObjectAt(1)->Id(), second);
}

TEST(WorldTest, InactiveObjectHasNoCollision)
{
    SceneData level;
    level.objects.push_back(NS::Game::Level::MakeCellObject(0, 0, 0));
    NS::Object::EnsureUniqueObjectIds(level);
    level.objects[0].active = false;

    NS::Object::Scene scene;
    scene.CreateSceneSubsystems();
    NS::Physics::PhysicsWorld physics;
    NS::Object::AssetManager assets{std::filesystem::path{"."}};
    World world;
    world.Rebuild(level, scene, physics, MakeFactory(assets, level));

    ASSERT_EQ(world.ObjectCount(), std::size_t{1});
    EXPECT_FALSE(world.ObjectAt(0)->IsActiveSelf());
    // 札の下りた配置物はすり抜ける
    EXPECT_TRUE(physics.IsEmpty());
}

TEST(WorldTest, TriggerBoxHasNoSolidCollision)
{
    SceneData level;
    NS::Object::ObjectData object{};
    nlohmann::json box = NS::Object::MakeComponentEntry("BoxColliderComponent");
    NS::Object::SetField(box, "Is Trigger", true);
    object.components = nlohmann::json::array({std::move(box)});
    level.objects.push_back(std::move(object));
    NS::Object::EnsureUniqueObjectIds(level);

    NS::Object::Scene scene;
    scene.CreateSceneSubsystems();
    NS::Physics::PhysicsWorld physics;
    NS::Object::AssetManager assets{std::filesystem::path{"."}};
    World world;
    world.Rebuild(level, scene, physics, MakeFactory(assets, level));

    ASSERT_EQ(world.ObjectCount(), std::size_t{1});
    // トリガの箱は通り抜ける体積。固形の当たりにも broadphase にも入らない
    EXPECT_TRUE(physics.IsEmpty());
}

TEST(WorldTest, RebuildClearsStalePhysicsAndBuildsNothingWithoutFactory)
{
    NS::Object::Scene scene;
    NS::Physics::PhysicsWorld physics;
    // 古い衝突が Rebuild 後に残ると、 編集で消した block へ当たり続ける。 必ず Clear から始まる仕様を固定する
    physics.AddAABB(NS::Math::AABB{NS::Math::Vector3{0.0f, 0.0f, 0.0f}, NS::Math::Vector3{1.0f, 1.0f, 1.0f}});
    physics.BuildBroadphase();
    ASSERT_FALSE(physics.IsEmpty());

    World world;
    const SceneData level{};
    world.Rebuild(level, scene, physics, nullptr);

    EXPECT_TRUE(physics.IsEmpty());
    EXPECT_EQ(world.ObjectCount(), 0u);
}

TEST(WorldTest, ClearEmptiesEverything)
{
    World world;
    world.Clear();
    EXPECT_EQ(world.ObjectCount(), 0u);
}

// プレイヤー実体も他の配置物と同じ一本道で組まれ、 データが決めた id の解決で実体が引ける
TEST(WorldTest, RebuildBuildsPlayerAndResolvesItById)
{
    SceneData level;
    level.objects.push_back(NS::Game::Level::MakeCellObject(0, 0, 0));
    level.objects.push_back(MakePlayerObject(NS::Math::Vector3{0.0f, 1.41f, 0.0f}, NS::Math::Quaternion{}));
    NS::Object::EnsureUniqueObjectIds(level);

    NS::Object::Scene scene;
    scene.CreateSceneSubsystems();
    NS::Physics::PhysicsWorld physics;
    NS::Object::AssetManager assets{std::filesystem::path{"."}};
    World world;
    world.Rebuild(level, scene, physics, MakeFactory(assets, level));

    // grid block とプレイヤーの両方が組まれ、 id 解決は所有リスト内の実体を指す
    ASSERT_EQ(world.ObjectCount(), 2u);
    const std::size_t playerIndex = FindPlayerObjectIndex(level);
    ASSERT_NE(playerIndex, NS::Object::k_NoObjectIndex);
    NS::Object::GameObject* resolved = world.FindObject(NS::Object::ObjectRef{level.objects[playerIndex].objectId});
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved, world.ObjectAt(1));
    EXPECT_FLOAT_EQ(resolved->Root().Position().y, 1.41f);
}

// 追従カメラの配置物は Rebuild 後も休止のまま問い合わせで引け、Target 参照が実体へ解決される
// 参照先より前に並ぶ前方参照でも、組み立てを先に済ませてから開始する二段組みで解決できる
TEST(WorldTest, RebuildBakesFollowCameraAndResolvesTarget)
{
    SceneData level;
    level.objects.push_back(NS::Game::Level::MakeFollowCameraObject(0u));
    level.objects.push_back(NS::Game::Level::MakeCellObject(0, 0, 0));
    NS::Object::EnsureUniqueObjectIds(level);
    // 追従先は自分より後ろに並ぶ grid block。Target 参照を採番後の実 id へ差し替える
    for (nlohmann::json& component : level.objects[0].components)
        if (NS::Object::HasField(component, "Target"))
            NS::Object::SetField(component, "Target", NS::Object::ObjectRef{level.objects[1].objectId});

    NS::Object::Scene scene;
    scene.CreateSceneSubsystems();
    NS::Physics::PhysicsWorld physics;
    NS::Object::AssetManager assets{std::filesystem::path{"."}};
    // 参照の解決は scene 越しに world へ届くので、 組むのは scene 自身の world
    NS::Object::World& world = scene.World();
    world.Rebuild(level, scene, physics, MakeFactory(assets, level));

    const auto follows = Collect<NS::Object::ThirdPersonFollowComponent>(world);
    ASSERT_EQ(follows.size(), 1u);
    auto* follow = follows[0];
    EXPECT_FALSE(follow->IsActive());
    // Brain 登録が使う抽象基底の問い合わせでも同じ実体が引ける
    const auto vcams = Collect<NS::Object::VirtualCameraComponent>(world);
    ASSERT_EQ(vcams.size(), 1u);
    EXPECT_EQ(vcams[0], follow);
    // データの Far Plane 100 が反射 set で効いている
    EXPECT_FLOAT_EQ(follow->FarPlane(), 100.0f);

    // 追従先の解決: world が組んだ grid block の Root を指す
    ASSERT_EQ(world.ObjectCount(), 2u);
    EXPECT_EQ(follow->Target(), &world.ObjectAt(1)->Root());
}

// component にも永続 id が振られ、object と同じ番号空間で誰とも重ならない
TEST(WorldTest, EnsureUniqueObjectIdsNumbersComponents)
{
    SceneData level;
    level.objects.push_back(NS::Game::Level::MakeCellObject(0, 0, 0));
    level.objects.push_back(NS::Game::Level::MakeCellObject(1, 0, 0));

    NS::Object::EnsureUniqueObjectIds(level);

    std::vector<std::uint32_t> ids;
    for (const NS::Object::ObjectData& object : level.objects)
    {
        ids.push_back(object.objectId);
        for (const nlohmann::json& entry : object.components)
            ids.push_back(NS::Object::ComponentEntryId(entry));
    }

    // 0 (未採番) が残っていない
    for (const std::uint32_t id : ids)
        EXPECT_NE(id, 0u);
    // object と component をまたいで重複が無い
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(std::adjacent_find(ids.begin(), ids.end()), ids.end());
}

// データの id が実体へ焼かれ、保存で往復しても同じ番号のまま
TEST(WorldTest, ComponentIdSurvivesBuildAndCapture)
{
    SceneData level;
    level.objects.push_back(NS::Game::Level::MakeCellObject(0, 0, 0));
    NS::Object::EnsureUniqueObjectIds(level);

    NS::Object::Scene scene;
    scene.CreateSceneSubsystems();
    NS::Physics::PhysicsWorld physics;
    NS::Object::AssetManager assets{std::filesystem::path{"."}};
    World world;
    world.Rebuild(level, scene, physics, MakeFactory(assets, level));

    ASSERT_EQ(world.ObjectCount(), 1u);
    NS::Object::GameObject* live = world.ObjectAt(0);

    // データに書かれた id がそのまま実体に載っている
    const std::uint32_t dataId = NS::Object::ComponentEntryId(level.objects[0].components[0]);
    ASSERT_NE(dataId, 0u);
    const std::string_view dataType = NS::Object::ComponentEntryType(level.objects[0].components[0]);
    NS::Object::Component* matched = nullptr;
    for (NS::Object::Component* comp : live->Components())
    {
        if (comp != nullptr && comp->Id() == dataId)
            matched = comp;
    }
    ASSERT_NE(matched, nullptr);
    EXPECT_EQ(matched->GetReflection()->typeName, dataType);

    // 実体から書き戻しても番号は変わらない。落ちると保存のたびに振り直しになる
    const NS::Object::ObjectData captured = NS::Object::CaptureObjectData(*live);
    bool found = false;
    for (const nlohmann::json& entry : captured.components)
    {
        if (NS::Object::ComponentEntryId(entry) == dataId)
            found = true;
    }
    EXPECT_TRUE(found);
}

// 組み直さずに 1 体消しても、破棄済みの実体が id 解決で引けない
TEST(WorldTest, RemoveByObjectIdDropsIdResolution)
{
    SceneData level;
    level.objects.push_back(NS::Game::Level::MakeCellObject(0, 0, 0));
    level.objects.push_back(NS::Game::Level::MakeCellObject(1, 0, 0));
    NS::Object::EnsureUniqueObjectIds(level);
    const std::uint32_t victimId = level.objects[1].objectId;

    NS::Object::Scene scene;
    scene.CreateSceneSubsystems();
    NS::Physics::PhysicsWorld physics;
    NS::Object::AssetManager assets{std::filesystem::path{"."}};
    World world;
    world.Rebuild(level, scene, physics, MakeFactory(assets, level));

    ASSERT_NE(world.FindObject(NS::Object::ObjectRef{victimId}), nullptr);

    world.RemoveByObjectId(victimId, physics);

    EXPECT_EQ(world.ObjectCount(), 1u);
    EXPECT_EQ(world.FindObject(NS::Object::ObjectRef{victimId}), nullptr);
}

// 1 体消したら当たり箱もその場で減る。 消えた物に当たり続けない
TEST(WorldTest, RemoveByObjectIdDropsCollider)
{
    SceneData level;
    level.objects.push_back(NS::Game::Level::MakeCellObject(0, 0, 0));
    level.objects.push_back(NS::Game::Level::MakeCellObject(1, 0, 0));
    NS::Object::EnsureUniqueObjectIds(level);
    const std::uint32_t victimId = level.objects[1].objectId;

    NS::Object::Scene scene;
    scene.CreateSceneSubsystems();
    NS::Physics::PhysicsWorld physics;
    NS::Object::AssetManager assets{std::filesystem::path{"."}};
    World world;
    world.Rebuild(level, scene, physics, MakeFactory(assets, level));

    const std::size_t before = physics.Aabbs().size();
    ASSERT_EQ(before, 2u);

    world.RemoveByObjectId(victimId, physics);

    EXPECT_EQ(physics.Aabbs().size(), 1u);
}

// 据え置きカメラの配置物は問い合わせで引け、エリア外の非アクティブで組み上がる
TEST(WorldTest, RebuildBakesPlacedCamerasInactive)
{
    SceneData level;
    NS::Object::ObjectData cameraObject{};
    NS::Object::SetObjectPosition(cameraObject, NS::Math::Vector3{8.0f, 0.0f, 0.0f});
    nlohmann::json comp = NS::Object::MakeComponentEntry("PlacedVirtualCamera");
    NS::Object::SetField(comp, "Priority", 20);
    cameraObject.components.push_back(std::move(comp));
    level.objects.push_back(std::move(cameraObject));

    NS::Object::Scene scene;
    NS::Physics::PhysicsWorld physics;
    NS::Object::AssetManager assets{std::filesystem::path{"."}};
    World world;
    world.Rebuild(level, scene, physics, MakeFactory(assets, level));

    const auto placedCameras = Collect<NS::Object::PlacedVirtualCamera>(world);
    ASSERT_EQ(placedCameras.size(), 1u);
    auto* placed = placedCameras[0];
    EXPECT_FALSE(placed->IsActive());
    EXPECT_EQ(placed->VcamPriority(), 20);
    // 視点位置は object の Transform から来る
    EXPECT_FLOAT_EQ(placed->ViewPosition().x, 8.0f);
}

// 指定した帯だけが回る。 帯をどの順で回すかは呼ぶ側の並びで決まる
TEST(WorldTest, UpdateObjectsRunsOnlyRequestedBand)
{
    World world;
    std::vector<int> order;
    auto* mover = world.Spawn<NS::Object::GameObject>();
    mover->AddComponent<BandRecordingComponent>(NS::Object::TickPriority::EarlyUpdate, &order, 1);
    mover->AddComponent<BandRecordingComponent>(NS::Object::TickPriority::Update, &order, 2);
    auto* rules = world.Spawn<NS::Object::GameObject>();
    rules->AddComponent<BandRecordingComponent>(NS::Object::TickPriority::Update + 100, &order, 3);
    rules->AddComponent<BandRecordingComponent>(NS::Object::TickPriority::LateUpdate, &order, 4);
    auto* camera = world.Spawn<NS::Object::GameObject>();
    camera->AddComponent<BandRecordingComponent>(NS::Object::TickPriority::LateUpdate + 50, &order, 5);

    // EarlyUpdate はすぐ上の Update を巻き込まない
    world.UpdateObjects(NS::Object::TickPriority::EarlyUpdate, NS::Object::TickPriority::Update);
    EXPECT_EQ(order, (std::vector<int>{1}));

    // Update は帯の途中 (+100) まで含み、 すぐ上の LateUpdate を巻き込まない
    world.UpdateObjects(NS::Object::TickPriority::Update, NS::Object::TickPriority::LateUpdate);
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));

    // LateUpdate は末尾の帯なので +50 の後方まで全部回る
    world.UpdateObjects(NS::Object::TickPriority::LateUpdate);
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3, 4, 5}));
}

// 同じ帯の中は配置物の並び順。 帯の途中の priority 値もその帯に含まれる
TEST(WorldTest, UpdateObjectsRunsSameBandInObjectOrder)
{
    World world;
    std::vector<int> order;
    auto first = std::make_unique<NS::Object::GameObject>();
    first->AddComponent<BandRecordingComponent>(NS::Object::TickPriority::Update, &order, 1);
    auto second = std::make_unique<NS::Object::GameObject>();
    second->AddComponent<BandRecordingComponent>(NS::Object::TickPriority::Update, &order, 2);
    second->AddComponent<BandRecordingComponent>(NS::Object::TickPriority::Update + 50, &order, 3);
    world.Append(std::move(first));
    world.Append(std::move(second));

    world.UpdateObjects(NS::Object::TickPriority::Update);
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

// 一時オブジェクトも配置物と同じ帯に乗る。 更新経路は 1 本で、 違いは保存・凍結に写らない事だけ
TEST(WorldTest, BandUpdatesIncludeTransientObjects)
{
    World world;
    std::vector<int> order;
    auto transient = std::make_unique<NS::Object::GameObject>();
    transient->SetTransient(true);
    transient->AddComponent<BandRecordingComponent>(NS::Object::TickPriority::Update, &order, 1);
    world.Append(std::move(transient));

    world.UpdateObjects(NS::Object::TickPriority::Update);
    EXPECT_EQ(order, (std::vector<int>{1}));
}

// 札の下りた component は帯の横断更新でも呼ばれない
TEST(WorldTest, BandUpdatesSkipInactiveComponents)
{
    World world;
    std::vector<int> order;
    auto obj = std::make_unique<NS::Object::GameObject>();
    auto* sleeping = obj->AddComponent<BandRecordingComponent>(NS::Object::TickPriority::Update, &order, 1);
    sleeping->SetActive(false);
    world.Append(std::move(obj));

    world.UpdateObjects(NS::Object::TickPriority::Update);
    EXPECT_TRUE(order.empty());
}

// 通常プレイが通る一括更新。 札の下りた component はここでも回らない
TEST(WorldTest, UpdateAllObjectsSkipsInactiveComponent)
{
    World world;
    auto* counter = world.Spawn<NS::Object::GameObject>()->AddComponent<CountingComponent>();

    counter->SetActive(false);
    world.UpdateAllObjects();
    EXPECT_EQ(counter->Count(), 0);

    counter->SetActive(true);
    world.UpdateAllObjects();
    EXPECT_EQ(counter->Count(), 1);
}

// 持ち主の札を下ろすと配下 component が一括更新から外れ、 戻せばまた回る
TEST(WorldTest, UpdateAllObjectsFollowsOwnerActiveFlag)
{
    World world;
    auto* owner = world.Spawn<NS::Object::GameObject>();
    auto* counter = owner->AddComponent<CountingComponent>();

    owner->SetActive(false);
    world.UpdateAllObjects();
    EXPECT_EQ(counter->Count(), 0);

    owner->SetActive(true);
    world.UpdateAllObjects();
    EXPECT_EQ(counter->Count(), 1);
}

// 取り込み形状の三角形も物理へ入る。 組み直し側が形状を名指ししない事の裏取り
TEST(WorldTest, MeshColliderTrianglesReachPhysics)
{
    // CCW 並びで法線が上を向く床の三角形。 斜辺を x+z=4 まで押し出し、 原点を縁でなく内側に置く
    std::vector<NS::Physics::Triangle> tris = {NS::Physics::Triangle{NS::Math::Vector3{-4.0f, 0.0f, -4.0f},
                                                                     NS::Math::Vector3{-4.0f, 0.0f, 8.0f},
                                                                     NS::Math::Vector3{8.0f, 0.0f, -4.0f}}};

    World world;
    auto* floor = world.Spawn<NS::Object::GameObject>();
    floor->AddComponent<NS::Object::MeshColliderComponent>(std::move(tris));

    NS::Physics::PhysicsWorld physics;
    world.RebuildPhysics(physics);
    ASSERT_FALSE(physics.IsEmpty());

    NS::Physics::Capsule cap;
    cap.center = NS::Math::Vector3{0.0f, 2.0f, 0.0f};
    const NS::Physics::SweepHit hit = physics.SweepCapsule(cap, NS::Math::Vector3{0.0f, -3.0f, 0.0f});
    EXPECT_TRUE(hit.hit);
    EXPECT_GT(hit.normal.y, 0.7f);
}

// 箱の入れ先は回転で決まる。 90° 刻みは AABB、 傾いていれば OBB
TEST(WorldTest, BoxColliderRoutesByRotation)
{
    World aligned;
    aligned.Spawn<NS::Object::GameObject>()->AddComponent<NS::Object::BoxColliderComponent>();

    NS::Physics::PhysicsWorld alignedPhysics;
    aligned.RebuildPhysics(alignedPhysics);
    EXPECT_EQ(alignedPhysics.Aabbs().size(), 1u);

    World tilted;
    auto* box = tilted.Spawn<NS::Object::GameObject>()->AddComponent<NS::Object::BoxColliderComponent>();
    box->SetRotationEulerDegrees(NS::Math::Vector3{0.0f, 30.0f, 0.0f});

    NS::Physics::PhysicsWorld tiltedPhysics;
    tilted.RebuildPhysics(tiltedPhysics);
    EXPECT_TRUE(tiltedPhysics.Aabbs().empty());
    EXPECT_FALSE(tiltedPhysics.IsEmpty());
}

// 帯を回すたびの確保と並べ替えが 1 フレームの予算をどれだけ食うかを測る
// 時間で合否を決めると環境差で揺れるので、 数字を出すだけにして判断は人が行う
TEST(WorldTest, UpdateObjectsCostMeasurement)
{
    const auto measure = [](std::size_t objectCount, std::size_t componentsPerObject) {
        World world;
        for (std::size_t i = 0; i < objectCount; ++i)
        {
            NS::Object::GameObject* obj = world.Spawn<NS::Object::GameObject>();
            for (std::size_t c = 0; c < componentsPerObject; ++c)
                obj->AddComponent<CountingComponent>();
        }

        constexpr int k_Iterations = 1000;
        const auto start = std::chrono::steady_clock::now();
        for (int n = 0; n < k_Iterations; ++n)
            world.UpdateObjects(NS::Object::TickPriority::Update);
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
