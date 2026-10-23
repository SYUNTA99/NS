#include "Editor/CategoryPalette.h"
#include "Editor/EditorActors.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PlacementCatalog.h"
#include "Editor/Undo/ActorSnapshotApplier.h"
#include "Game/Level/PropActor.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/MeshCollision.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Windows/Filesystem.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include "NSlib/Core/Types.h"
#include <optional>
#include <string_view>

// エディタの配置 (ヒエラルキーの追加メニュー・メッシュの投げ込み) が、SceneActor のクラスごと置くことを縛る

TEST(EditorPlacement, PlaceItemSpawnsActorOfItsClassAndSelectsIt)
{
    NS::Scene scene;
    LevelEditorController editor(&scene);
    const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("PropActor");
    ASSERT_TRUE(item.has_value());

    editor.PlaceItem(*item);

    const NS::uint32 id = editor.SelectedActorId();
    ASSERT_NE(id, NS::k_NoActorId);
    NS::SceneActor* placed = scene.Actors().FindByActorId(id);
    ASSERT_NE(placed, nullptr);
    EXPECT_EQ(std::string_view{placed->ClassName()}, "PropActor");
}

TEST(EditorPlacement, PlacedPartsUseTheItemsMesh)
{
    NS::Scene scene;
    LevelEditorController editor(&scene);
    const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem(NS::Editor::k_PartsSlopeLabel);
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

// 坂の角度は組み込みの形の表だけが持つ。ひな形とパレットのブラシは、その表の角度を読む
TEST(EditorPlacement, SlopeAnglesComeFromTheBuiltinShapes)
{
    for (const std::string_view name : {"wedge45", "wedge30", "wedge22", "wedge15"})
    {
        const std::optional<float> angle = NS::AssetManager::BuiltinSlopeAngleDegrees(name);
        ASSERT_TRUE(angle.has_value()) << name;
        EXPECT_FLOAT_EQ(NS::Editor::PartsSlopeAngleDegrees(NS::Editor::MakeMeshPartsPrototype(name)), *angle)
            << name;
    }
    EXPECT_FALSE(NS::AssetManager::BuiltinSlopeAngleDegrees("cube").has_value());

    NS::Editor::CategoryPalette palette;
    palette.SetActiveSlot(1);
    EXPECT_FLOAT_EQ(palette.CurrentSlopeAngleDegrees(), *NS::AssetManager::BuiltinSlopeAngleDegrees("wedge45"));
}

// エディタの姿勢の関数で JSON に書いた姿が、そのまま組んだアクターの根の姿になる
TEST(EditorPlacement, PoseFunctionsWriteTheJsonTheActorReads)
{
    nlohmann::json bare = NS::MakeActorJson();
    EXPECT_FLOAT_EQ(NS::Editor::ActorJsonScale(bare).y, 1.0f);
    NS::Editor::EnsureTransformClassObj(bare);
    EXPECT_FLOAT_EQ(NS::Editor::ActorJsonScale(bare).y, 1.0f);
    EXPECT_FLOAT_EQ(NS::Editor::ActorJsonPosition(bare).y, 0.0f);

    nlohmann::json object = NS::Editor::MakePrototypeJson<GL::Level::PropActor>();
    const NS::Quaternion rotation = NS::Quaternion::CreateFromYawPitchRoll(0.5f, 0.0f, 0.0f);
    NS::Editor::SetActorJsonPosition(object, NS::Vector3{1.0f, 2.0f, 3.0f});
    NS::Editor::SetActorJsonRotation(object, rotation);
    NS::Editor::SetActorJsonScale(object, NS::Vector3{2.0f, 2.0f, 2.0f});
    EXPECT_FLOAT_EQ(NS::Editor::ActorJsonPosition(object).z, 3.0f);
    EXPECT_FLOAT_EQ(NS::Editor::ActorJsonRotation(object).y, rotation.y);
    EXPECT_FLOAT_EQ(NS::Editor::ActorJsonScale(object).x, 2.0f);

    NS::Scene scene;
    NS::SceneActor* actor = scene.SpawnFromJson(object);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(actor->Root().Position().z, 3.0f);
    EXPECT_FLOAT_EQ(actor->Root().Rotation().y, rotation.y);
    EXPECT_FLOAT_EQ(actor->Root().Scale().x, 2.0f);
}

TEST(EditorPlacement, UndoRemovesPlacedActor)
{
    NS::Scene scene;
    LevelEditorController editor(&scene);
    const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("GoalActor");
    ASSERT_TRUE(item.has_value());

    editor.PlaceItem(*item);
    const NS::uint32 id = editor.SelectedActorId();
    ASSERT_NE(scene.Actors().FindByActorId(id), nullptr);

    NS::Editor::ActorSnapshotApplier applier{&scene};
    ASSERT_TRUE(editor.Editor().Undo().Undo(applier));
    EXPECT_EQ(scene.Actors().FindByActorId(id), nullptr);

    // やり直すと同じ id・同じクラスで戻る
    ASSERT_TRUE(editor.Editor().Undo().Redo(applier));
    NS::SceneActor* again = scene.Actors().FindByActorId(id);
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

// 1 m の升として数えるのは組み込みの立方体と坂だけ。投げ込んだメッシュは大きさが分からないので数えない
TEST(EditorPlacement, OnlyTheBuiltInPartsCountAsCells)
{
    NS::Scene scene;
    LevelEditorController editor(&scene);
    const std::optional<NS::Editor::PlacementItem> cube = NS::Editor::FindPlacementItem(NS::Editor::k_PartsCubeLabel);
    ASSERT_TRUE(cube.has_value());
    editor.PlaceItem(*cube);
    const NS::SceneActor* placedCube = scene.Actors().FindByActorId(editor.SelectedActorId());
    ASSERT_NE(placedCube, nullptr);
    editor.AddMeshParts(NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::Combine(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), "Models"),
        "__ns_missing_terrain__.glb"));
    const NS::SceneActor* dropped = scene.Actors().FindByActorId(editor.SelectedActorId());
    ASSERT_NE(dropped, nullptr);

    EXPECT_TRUE(NS::Editor::IsCellBrushActor(*placedCube));
    EXPECT_FALSE(NS::Editor::IsCellBrushActor(*dropped));
}
