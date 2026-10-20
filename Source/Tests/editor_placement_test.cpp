#include "Editor/EditorObjects.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PlacementCatalog.h"
#include "Editor/Undo/ObjectSnapshotApplier.h"
#include "NSlib/CoreObject/ClassObjs/MeshCollision.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Windows/Filesystem.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include "NSlib/Core/Types.h"
#include <string_view>

// エディタの配置 (ヒエラルキーの追加メニュー・メッシュの投げ込み) が、SceneActor のクラスごと置くことを縛る

TEST(EditorPlacement, PlaceItemSpawnsActorOfItsClassAndSelectsIt)
{
    NS::Scene scene;
    LevelEditorController editor(&scene);
    const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("PropActor");
    ASSERT_NE(item, nullptr);

    editor.PlaceItem(*item);

    const NS::uint32 id = editor.SelectedObjectId();
    ASSERT_NE(id, NS::k_NoObjectId);
    NS::SceneActor* placed = scene.Objects().FindByObjectId(id);
    ASSERT_NE(placed, nullptr);
    EXPECT_EQ(std::string_view{placed->ClassName()}, "PropActor");
}

TEST(EditorPlacement, PlacedPartsUseTheItemsMesh)
{
    NS::Scene scene;
    LevelEditorController editor(&scene);
    const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem(NS::Editor::k_PartsSlopeLabel);
    ASSERT_NE(item, nullptr);

    editor.PlaceItem(*item);

    NS::SceneActor* placed = scene.Objects().FindByObjectId(editor.SelectedObjectId());
    ASSERT_NE(placed, nullptr);
    EXPECT_EQ(std::string_view{placed->ClassName()}, "MapPartsActor");
    const NS::Model* renderer = placed->ModelClassObj();
    ASSERT_NE(renderer, nullptr);
    EXPECT_EQ(renderer->MeshRef(), "wedge45");
    // 当たりは見た目のメッシュに付いて来る
    EXPECT_NE(NS::Cast<NS::MeshCollision>(placed->CollisionClassObj()), nullptr);
}

TEST(EditorPlacement, UndoRemovesPlacedActor)
{
    NS::Scene scene;
    LevelEditorController editor(&scene);
    const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("GoalActor");
    ASSERT_NE(item, nullptr);

    editor.PlaceItem(*item);
    const NS::uint32 id = editor.SelectedObjectId();
    ASSERT_NE(scene.Objects().FindByObjectId(id), nullptr);

    NS::Editor::ObjectSnapshotApplier applier{&scene};
    ASSERT_TRUE(editor.Editor().Undo().Undo(applier));
    EXPECT_EQ(scene.Objects().FindByObjectId(id), nullptr);

    // やり直すと同じ id・同じクラスで戻る
    ASSERT_TRUE(editor.Editor().Undo().Redo(applier));
    NS::SceneActor* again = scene.Objects().FindByObjectId(id);
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(std::string_view{again->ClassName()}, "GoalActor");
}

// この試しは置いた物の種類と欄の名前だけを見るので、実在しない file で足りる
TEST(EditorPlacement, DroppedMeshBecomesMapPartsWithMeshCollision)
{
    NS::Scene scene;
    LevelEditorController editor(&scene);

    const TestLogCapture log;
    editor.AddMeshParts(NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::Combine(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), "Models"),
        "__ns_missing_terrain__.glb"));
    EXPECT_EQ(log.ErrorCount(), 2);

    NS::SceneActor* placed = scene.Objects().FindByObjectId(editor.SelectedObjectId());
    ASSERT_NE(placed, nullptr);
    EXPECT_EQ(std::string_view{placed->ClassName()}, "MapPartsActor");
    const NS::Model* renderer = placed->ModelClassObj();
    ASSERT_NE(renderer, nullptr);
    EXPECT_EQ(renderer->MeshRef(), "Assets/Models/__ns_missing_terrain__.glb");
    EXPECT_NE(NS::Cast<NS::MeshCollision>(placed->CollisionClassObj()), nullptr);
    // 名前はファイル名から付く
    EXPECT_EQ(placed->Name(), "__ns_missing_terrain__");
}

// 1 m の升として数えるのは組み込みの立方体と坂だけ。投げ込んだメッシュは大きさが分からないので数えない
TEST(EditorPlacement, OnlyTheBuiltInPartsCountAsCells)
{
    NS::Scene scene;
    LevelEditorController editor(&scene);
    const NS::Editor::PlacementItem* cube = NS::Editor::FindPlacementItem(NS::Editor::k_PartsCubeLabel);
    ASSERT_NE(cube, nullptr);
    editor.PlaceItem(*cube);
    const NS::SceneActor* placedCube = scene.Objects().FindByObjectId(editor.SelectedObjectId());
    ASSERT_NE(placedCube, nullptr);
    editor.AddMeshParts(NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::Combine(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), "Models"),
        "__ns_missing_terrain__.glb"));
    const NS::SceneActor* dropped = scene.Objects().FindByObjectId(editor.SelectedObjectId());
    ASSERT_NE(dropped, nullptr);

    EXPECT_TRUE(NS::Editor::IsCellBrushObject(*placedCube));
    EXPECT_FALSE(NS::Editor::IsCellBrushObject(*dropped));
}
