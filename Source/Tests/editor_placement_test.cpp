#include "Editor/EditorActors.h"
#include "Editor/EditorController.h"
#include "Editor/PlacementCatalog.h"
#include "Game/Level/GameScene.h"
#include "Game/Level/GoalActor.h"
#include "NSlib/App/Application.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Windows/Keyboard.h"
#include "NSlib/Core/Windows/Mouse.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/MeshCollision.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Gfx/Render/DebugDraw.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "TestActorFile.h"
#include "TestLogCapture.h"
#include "TestSpawn.h"
#include "Tests/TestCheckLayer.h"

#include <gtest/gtest.h>

#include "NSlib/Core/Types.h"
#include "Tests/TestHiddenApp.h"
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

using NS::Ed::EditorController;

// エディタの配置 (ヒエラルキーの追加メニュー・メッシュの投げ込み) が、SceneActor のクラスごと置くことを縛る

TEST(EditorPlacement, PlaceItemSpawnsActorOfItsClassAndSelectsIt)
{
    NS::Scene scene;
    EditorController editor(scene);
    const std::optional<NS::Ed::PlacementItem> item = NS::Ed::FindPlacementItem("GoalActor");
    ASSERT_TRUE(item.has_value());

    editor.PlaceItem(*item);

    const NS::uint32 id = editor.SelectedActorId();
    ASSERT_NE(id, NS::k_NoActorId);
    NS::SceneActor* placed = scene.Actors().FindByActorId(id);
    ASSERT_NE(placed, nullptr);
    EXPECT_EQ(std::string_view{placed->ClassName()}, "GoalActor");
}

TEST(EditorPlacement, PlacedPartsUseTheItemsMesh)
{
    NS::Scene scene;
    EditorController editor(scene);
    const std::optional<NS::Ed::PlacementItem> item = NS::Ed::FindPlacementItem(NS::Ed::k_PartsSlopeLabel);
    ASSERT_TRUE(item.has_value());

    editor.PlaceItem(*item);

    NS::SceneActor* placed = scene.Actors().FindByActorId(editor.SelectedActorId());
    ASSERT_NE(placed, nullptr);
    EXPECT_EQ(std::string_view{placed->ClassName()}, "MapPartsActor");
    const NS::Model* renderer = placed->ModelClassObj();
    ASSERT_NE(renderer, nullptr);
    EXPECT_EQ(renderer->MeshRef(), "wedge45");
    // 当たりは見た目のメッシュに付いて来る
    EXPECT_NE(NS::Cast<NS::MeshCollision>(placed->CollisionClassObj()), nullptr);
}

// エディタの姿勢の関数で JSON に書いた姿が、そのまま組んだアクターの根の姿になる
TEST(EditorPlacement, PoseFunctionsWriteTheJsonTheActorReads)
{
    nlohmann::json bare = NS::MakeActorJson();
    EXPECT_FLOAT_EQ(NS::Ed::ActorJsonScale(bare).y, 1.0f);
    NS::Ed::EnsureTransformClassObj(bare);
    EXPECT_FLOAT_EQ(NS::Ed::ActorJsonScale(bare).y, 1.0f);
    EXPECT_FLOAT_EQ(NS::Ed::ActorJsonPosition(bare).y, 0.0f);

    nlohmann::json object = NS::Ed::MakePrototypeJson<GL::Level::GoalActor>();
    const NS::Quaternion rotation = NS::Quaternion::CreateFromYawPitchRoll(0.5f, 0.0f, 0.0f);
    NS::Ed::SetActorJsonPosition(object, NS::Vector3{1.0f, 2.0f, 3.0f});
    NS::Ed::SetActorJsonRotation(object, rotation);
    NS::Ed::SetActorJsonScale(object, NS::Vector3{2.0f, 2.0f, 2.0f});
    EXPECT_FLOAT_EQ(NS::Ed::ActorJsonPosition(object).z, 3.0f);
    EXPECT_FLOAT_EQ(NS::Ed::ActorJsonRotation(object).y, rotation.y);
    EXPECT_FLOAT_EQ(NS::Ed::ActorJsonScale(object).x, 2.0f);

    NS::Scene scene;
    NS::SceneActor* actor = scene.SpawnFromJson(object);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(actor->TransformObj().Position().z, 3.0f);
    EXPECT_FLOAT_EQ(actor->TransformObj().Rotation().y, rotation.y);
    EXPECT_FLOAT_EQ(actor->TransformObj().Scale().x, 2.0f);
}

TEST(EditorPlacement, UndoRemovesPlacedActor)
{
    NS::Scene scene;
    EditorController editor(scene);
    const std::optional<NS::Ed::PlacementItem> item = NS::Ed::FindPlacementItem("GoalActor");
    ASSERT_TRUE(item.has_value());

    editor.PlaceItem(*item);
    const NS::uint32 id = editor.SelectedActorId();
    const NS::SceneActor* const placed = scene.Actors().FindByActorId(id);
    ASSERT_NE(placed, nullptr);

    ASSERT_TRUE(editor.Editor().Undo().Undo());
    EXPECT_EQ(scene.Actors().FindByActorId(id), nullptr);

    // やり直すと、履歴が持っていた同じアクターが戻る
    ASSERT_TRUE(editor.Editor().Undo().Redo());
    EXPECT_EQ(scene.Actors().FindByActorId(id), placed);
}

// この試しは置いた物の種類と欄の名前だけを見るので、実在しない file で足りる
TEST(EditorPlacement, DroppedMeshBecomesMapPartsWithMeshCollision)
{
    NS::Scene scene;
    EditorController editor(scene);

    const TestLogCapture log;
    editor.AddMeshParts(NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::Combine(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), "Models"),
        "__ns_missing_terrain__.glb"));
    EXPECT_EQ(log.ErrorCount(), 2);

    NS::SceneActor* placed = scene.Actors().FindByActorId(editor.SelectedActorId());
    ASSERT_NE(placed, nullptr);
    EXPECT_EQ(std::string_view{placed->ClassName()}, "MapPartsActor");
    const NS::Model* renderer = placed->ModelClassObj();
    ASSERT_NE(renderer, nullptr);
    EXPECT_EQ(renderer->MeshRef(), "Assets/Models/__ns_missing_terrain__.glb");
    EXPECT_NE(NS::Cast<NS::MeshCollision>(placed->CollisionClassObj()), nullptr);
    // 名前はファイル名から付く
    EXPECT_EQ(placed->Name(), "__ns_missing_terrain__");
}

namespace
{
    // 消したアクターに OnEndPlay を呼んで解放した時機を見るためのゴール。
    // 数えるのは counted を立てた物だけで、JSON から作り直した PlacementCountedGoal は数えない
    class PlacementCountedGoal final : public GL::Level::GoalActor
    {
    public:
        NS_REFLECT_NONE(PlacementCountedGoal, GL::Level::GoalActor)

        ~PlacementCountedGoal() override
        {
            if (counted)
            {
                ++s_destroyed;
            }
        }

        void OnEndPlay() override
        {
            if (counted)
            {
                ++s_endPlays;
            }
            GL::Level::GoalActor::OnEndPlay();
        }

        bool counted = false;
        static int s_endPlays;
        static int s_destroyed;
    };

    int PlacementCountedGoal::s_endPlays = 0;
    int PlacementCountedGoal::s_destroyed = 0;

    // GoalActor の .actor をクラスだけ替えて PlacementCountedGoal の .actor に敷き、数える PlacementCountedGoal を作って id を返す
    // 敷いた .actor は outScope が試しの終わりに外す
    NS::uint32 SpawnCountedGoal(NS::Scene& scene, std::optional<ScopedActorFile>& outScope)
    {
        PlacementCountedGoal::s_endPlays = 0;
        PlacementCountedGoal::s_destroyed = 0;
        const nlohmann::json* goal = NS::ActorFileLibrary::Get().Find("GoalActor");
        EXPECT_NE(goal, nullptr);
        if (goal == nullptr)
        {
            return NS::k_NoActorId;
        }
        nlohmann::json actorFile = *goal;
        NS::SetActorFileClass(actorFile, "PlacementCountedGoal");
        outScope.emplace("PlacementCountedGoal", std::move(actorFile));
        PlacementCountedGoal* spawned = SpawnFromActorFile<PlacementCountedGoal>(scene, "PlacementCountedGoal", "GoalActor");
        EXPECT_NE(spawned, nullptr);
        if (spawned == nullptr)
        {
            return NS::k_NoActorId;
        }
        spawned->counted = true;
        return spawned->Id();
    }
} // namespace

NS_CLASS(PlacementCountedGoal)

// 置く・消す は、どれも未保存にする。読み込んだだけのシーンは未保存でない
TEST(EditorUnsaved, PlaceAndDeleteMarkTheSceneUnsaved)
{
    {
        NS::Scene scene;
        EditorController editor(scene);
        ASSERT_FALSE(editor.Editor().HasUnsavedChanges());
        const std::optional<NS::Ed::PlacementItem> item = NS::Ed::FindPlacementItem("GoalActor");
        ASSERT_TRUE(item.has_value());
        editor.PlaceItem(*item);
        EXPECT_TRUE(editor.Editor().HasUnsavedChanges());
    }
    {
        NS::Scene scene;
        const NS::uint32 id = SpawnFromActorFile<GL::Level::GoalActor>(scene, "GoalActor")->Id();
        EditorController editor(scene);
        ASSERT_FALSE(editor.Editor().HasUnsavedChanges());
        editor.SelectActorById(id);
        editor.DeleteSelectedActor();
        EXPECT_EQ(scene.Actors().FindByActorId(id), nullptr);
        EXPECT_TRUE(editor.Editor().HasUnsavedChanges());
    }
}

// エディタで消したアクターは一覧から無くなり、取り消しの履歴が持つ。元に戻すと同じアクターが戻り、
// 履歴を捨てた時に OnEndPlay を 1 回呼んで解放する
TEST(EditorUnsaved, DeletedActorIsHeldByTheHistoryUntilItIsDiscarded)
{
    std::optional<ScopedActorFile> scope;
    NS::Scene scene;
    const NS::uint32 id = SpawnCountedGoal(scene, scope);
    ASSERT_NE(id, NS::k_NoActorId);
    const NS::SceneActor* const original = scene.Actors().FindByActorId(id);
    EditorController editor(scene);
    editor.SelectActorById(id);

    editor.DeleteSelectedActor();
    EXPECT_EQ(scene.Actors().FindByActorId(id), nullptr);
    EXPECT_EQ(PlacementCountedGoal::s_endPlays, 0);
    EXPECT_EQ(PlacementCountedGoal::s_destroyed, 0);

    // JSON から作り直した物は counted が偽で、同じ番地に作られても見分けられる
    ASSERT_TRUE(editor.Editor().Undo().Undo());
    const PlacementCountedGoal* const restored =
        NS::Cast<PlacementCountedGoal>(scene.Actors().FindByActorId(id));
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored, original);
    EXPECT_TRUE(restored->counted);

    ASSERT_TRUE(editor.Editor().Undo().Redo());
    editor.Editor().Undo().Clear();
    EXPECT_EQ(scene.Actors().FindByActorId(id), nullptr);
    EXPECT_EQ(PlacementCountedGoal::s_endPlays, 1);
    EXPECT_EQ(PlacementCountedGoal::s_destroyed, 1);
}
