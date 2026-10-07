#include "Editor/EditorObjects.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PlacementCatalog.h"
#include "Editor/Undo/ObjectSnapshotApplier.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Components/MeshCollision.h"
#include "NSlib/Object/Components/Model.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Windows/Filesystem.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>

// エディタの配置 (ヒエラルキーの追加メニュー・メッシュの投げ込み) が、Actor のクラスごと置くことを縛る

TEST(EditorPlacement, PlaceItemSpawnsActorOfItsClassAndSelectsIt)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("置物");
    ASSERT_NE(item, nullptr);

    editor.PlaceItem(*item);

    const std::uint32_t id = editor.SelectedObjectId();
    ASSERT_NE(id, NS::Obj::k_NoObjectId);
    NS::Obj::Actor* placed = scene.Objects().FindByObjectId(id);
    ASSERT_NE(placed, nullptr);
    EXPECT_EQ(std::string_view{placed->ClassName()}, "MapObj");
}

TEST(EditorPlacement, PlacedPartsUseTheItemsMesh)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem(NS::Editor::k_PartsSlopeLabel);
    ASSERT_NE(item, nullptr);

    editor.PlaceItem(*item);

    NS::Obj::Actor* placed = scene.Objects().FindByObjectId(editor.SelectedObjectId());
    ASSERT_NE(placed, nullptr);
    EXPECT_EQ(std::string_view{placed->ClassName()}, "MapParts");
    const NS::Obj::Model* renderer = placed->ModelPart();
    ASSERT_NE(renderer, nullptr);
    EXPECT_EQ(renderer->MeshRef(), "wedge45");
    // 当たりは見た目のメッシュに付いて来る
    EXPECT_NE(NS::Obj::ComponentCast<NS::Obj::MeshCollision>(placed->Part("Collision")), nullptr);
}

TEST(EditorPlacement, UndoRemovesPlacedActor)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("ゴール");
    ASSERT_NE(item, nullptr);

    editor.PlaceItem(*item);
    const std::uint32_t id = editor.SelectedObjectId();
    ASSERT_NE(scene.Objects().FindByObjectId(id), nullptr);

    NS::Editor::ObjectSnapshotApplier applier{&scene};
    ASSERT_TRUE(editor.Editor().Undo().Undo(applier));
    EXPECT_EQ(scene.Objects().FindByObjectId(id), nullptr);

    // やり直すと同じ id・同じクラスで戻る
    ASSERT_TRUE(editor.Editor().Undo().Redo(applier));
    NS::Obj::Actor* again = scene.Objects().FindByObjectId(id);
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(std::string_view{again->ClassName()}, "Goal");
}

// 参照の実体化は AssetManager を差した Scene だけが行う。この試しは差していないので実在しない file で足りる
TEST(EditorPlacement, DroppedMeshBecomesMapPartsWithMeshCollision)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);

    editor.AddMeshParts(NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::Combine(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), "Models"),
        "__ns_missing_terrain__.glb"));

    NS::Obj::Actor* placed = scene.Objects().FindByObjectId(editor.SelectedObjectId());
    ASSERT_NE(placed, nullptr);
    EXPECT_EQ(std::string_view{placed->ClassName()}, "MapParts");
    const NS::Obj::Model* renderer = placed->ModelPart();
    ASSERT_NE(renderer, nullptr);
    EXPECT_EQ(renderer->MeshRef(), "Assets/Models/__ns_missing_terrain__.glb");
    EXPECT_NE(NS::Obj::ComponentCast<NS::Obj::MeshCollision>(placed->Part("Collision")), nullptr);
    // 名前はファイル名から付く
    EXPECT_EQ(placed->Name(), "__ns_missing_terrain__");
}

// 1 m の升として数えるのは組み込みの立方体と坂だけ。投げ込んだメッシュは大きさが分からないので数えない
TEST(EditorPlacement, OnlyTheBuiltInPartsCountAsCells)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    const NS::Editor::PlacementItem* cube = NS::Editor::FindPlacementItem(NS::Editor::k_PartsCubeLabel);
    ASSERT_NE(cube, nullptr);
    editor.PlaceItem(*cube);
    const NS::Obj::Actor* placedCube = scene.Objects().FindByObjectId(editor.SelectedObjectId());
    ASSERT_NE(placedCube, nullptr);
    editor.AddMeshParts(NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::Combine(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), "Models"),
        "__ns_missing_terrain__.glb"));
    const NS::Obj::Actor* dropped = scene.Objects().FindByObjectId(editor.SelectedObjectId());
    ASSERT_NE(dropped, nullptr);

    EXPECT_TRUE(NS::Editor::IsCellBrushObject(*placedCube));
    EXPECT_FALSE(NS::Editor::IsCellBrushObject(*dropped));
}
