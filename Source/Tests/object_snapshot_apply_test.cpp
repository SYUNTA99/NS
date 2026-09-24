#include "Editor/Undo/ObjectSnapshotApplier.h"
#include "Game/Level/Goal.h"
#include "Runtime/Object/Components/BoxCollider.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Scene/Scene.h"

#include <gtest/gtest.h>
#include <optional>
#include <utility>
#include <vector>

//! undo の 1 体適用が、値だけの違いなら実体を残して書き戻し、component の増減なら作り直すことを縛る
//! UE / Unity と同じく、値の undo でポインタと実行時の状態が入れ替わらないことが要

namespace
{
    nlohmann::json MakeRock(float x, float y, float z)
    {
        nlohmann::json object = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectPosition(object, NS::Core::Vector3{x, y, z});
        NS::Obj::ObjectJsonComponents(object).push_back(NS::Obj::MakeComponentEntry("BoxCollider"));
        return object;
    }

    // 岩を 1 つずつ置いたシーンを組み、置いた順に配置物を返す
    std::vector<NS::Obj::GameObject*> LoadRocks(NS::Obj::Scene& scene, std::size_t count)
    {
        nlohmann::json data = NS::Obj::MakeSceneJson();
        for (std::size_t i = 0; i < count; ++i)
        {
            NS::Obj::SceneJsonObjects(data).push_back(MakeRock(static_cast<float>(i), 0.0f, 0.0f));
        }
        scene.LoadJson(std::move(data));

        std::vector<NS::Obj::GameObject*> placed;
        for (NS::Obj::GameObject* obj : scene.Objects())
        {
            if (!obj->IsTransient())
                placed.push_back(obj);
        }
        return placed;
    }
} // namespace

TEST(ObjectSnapshotApply, ValueUndoKeepsTheSameObject)
{
    NS::Obj::Scene scene;
    NS::Editor::ObjectSnapshotApplier applier{&scene};
    std::vector<NS::Obj::GameObject*> rocks = LoadRocks(scene, 1);
    ASSERT_EQ(rocks.size(), 1u);
    NS::Obj::GameObject* rock = rocks[0];
    NS::Obj::BoxCollider* box = rock->FindComponent<NS::Obj::BoxCollider>();
    ASSERT_NE(box, nullptr);
    const std::uint32_t id = rock->Id();

    const std::optional<nlohmann::json> before = applier.CaptureObject(id);
    ASSERT_TRUE(before.has_value());
    rock->Root().SetPosition(NS::Core::Vector3{5.0f, 6.0f, 7.0f});
    scene.Objects().RenameObject(*rock, "Moved");

    applier.ApplyObjectSnapshot(id, before);

    // 実体も component も同じ物のまま、値だけが戻る
    EXPECT_EQ(scene.Objects().FindByObjectId(id), rock);
    EXPECT_EQ(rock->FindComponent<NS::Obj::BoxCollider>(), box);
    EXPECT_NEAR(rock->Root().Position().x, 0.0f, 1e-4f);
    EXPECT_NEAR(rock->Root().Position().y, 0.0f, 1e-4f);
    EXPECT_NEAR(rock->Root().Position().z, 0.0f, 1e-4f);
    EXPECT_EQ(rock->Name(), NS::Obj::ObjectJsonName(*before));
}

TEST(ObjectSnapshotApply, ParentUndoKeepsTheSameObjectAndLocalPose)
{
    NS::Obj::Scene scene;
    NS::Editor::ObjectSnapshotApplier applier{&scene};
    std::vector<NS::Obj::GameObject*> rocks = LoadRocks(scene, 2);
    ASSERT_EQ(rocks.size(), 2u);
    NS::Obj::GameObject* parent = rocks[0];
    NS::Obj::GameObject* child = rocks[1];
    const std::uint32_t childId = child->Id();

    const std::optional<nlohmann::json> before = applier.CaptureObject(childId);
    child->SetParent(parent);
    child->Root().SetPosition(NS::Core::Vector3{9.0f, 0.0f, 0.0f});
    const std::optional<nlohmann::json> after = applier.CaptureObject(childId);

    applier.ApplyObjectSnapshot(childId, before);
    EXPECT_EQ(scene.Objects().FindByObjectId(childId), child);
    EXPECT_EQ(child->Parent(), nullptr);
    EXPECT_NEAR(child->Root().Position().x, 1.0f, 1e-4f);

    // redo も同じ実体へ親と local を書き戻す
    applier.ApplyObjectSnapshot(childId, after);
    EXPECT_EQ(scene.Objects().FindByObjectId(childId), child);
    EXPECT_EQ(child->Parent(), parent);
    EXPECT_NEAR(child->Root().Position().x, 9.0f, 1e-4f);
}

TEST(ObjectSnapshotApply, ComponentCountChangeRebuildsTheObject)
{
    NS::Obj::Scene scene;
    NS::Editor::ObjectSnapshotApplier applier{&scene};
    std::vector<NS::Obj::GameObject*> rocks = LoadRocks(scene, 1);
    ASSERT_EQ(rocks.size(), 1u);
    const std::uint32_t id = rocks[0]->Id();

    const std::optional<nlohmann::json> before = applier.CaptureObject(id);
    ASSERT_TRUE(before.has_value());
    nlohmann::json withGoal = *before;
    NS::Obj::ObjectJsonComponents(withGoal).push_back(NS::Obj::MakeComponentEntry("Goal"));

    // component が増えた姿は作り直して入れる。id は保つ
    applier.ApplyObjectSnapshot(id, withGoal);
    NS::Obj::GameObject* rebuilt = scene.Objects().FindByObjectId(id);
    ASSERT_NE(rebuilt, nullptr);
    EXPECT_NE(rebuilt->FindComponent<NS::Game::Level::Goal>(), nullptr);
    EXPECT_NE(rebuilt->FindComponent<NS::Obj::BoxCollider>(), nullptr);

    applier.ApplyObjectSnapshot(id, before);
    NS::Obj::GameObject* restored = scene.Objects().FindByObjectId(id);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->FindComponent<NS::Game::Level::Goal>(), nullptr);
}
