#include "Editor/CategoryPalette.h"
#include "Editor/EditorActors.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PlacementCatalog.h"
#include "Editor/StageBuildTool.h"
#include "Editor/Undo/ActorSnapshotApplier.h"
#include "Game/Level/PropActor.h"
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
#include "TestLogCapture.h"
#include "Tests/TestCheckLayer.h"

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

    EXPECT_TRUE(NS::Editor::IsCellBrushActor(placedCube));
    EXPECT_FALSE(NS::Editor::IsCellBrushActor(dropped));
}

TEST(StageBuild, FreePlacementUsesPartDimensionsAboveTheEmptySpacePlane)
{
    NS::Scene scene;
    NS::Editor::StageBuildTool tool;
    tool.SelectPart(NS::Editor::StageBuildTool::Part::Wall);
    tool.Size() = {3.5f, 4.0f, 0.75f};
    tool.EmptySpaceHeight() = 2.0f;
    tool.YawDegrees() = 30.0f;
    const std::optional<NS::Vector3> center =
        tool.Placement(NS::Ray{NS::Vector3{1.25f, 20.0f, -2.75f}, NS::Vector3{0.0f, -1.0f, 0.0f}}, scene.Physics());
    ASSERT_TRUE(center);
    EXPECT_FLOAT_EQ(center->x, 1.25f);
    EXPECT_FLOAT_EQ(center->y, 4.0f);
    EXPECT_FLOAT_EQ(center->z, -2.75f);

    NS::SceneActor* actor = scene.SpawnFromJson(tool.MakeActor(*center));
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(std::string_view{actor->ClassName()}, "MapPartsActor");
    EXPECT_EQ(actor->ModelClassObj()->MeshRef(), "cube");
    EXPECT_FLOAT_EQ(actor->Root().Scale().x, 3.5f);
    EXPECT_FLOAT_EQ(actor->Root().Scale().z, 0.75f);
    EXPECT_NEAR(actor->Root().Rotation().y, std::sin(NS::DegreesToRadians(15.0f)), 0.0001f);
}

TEST(StageBuild, PlacementUsesTheActualSlopeRatherThanItsBounds)
{
    NS::Scene scene;
    nlohmann::json slope = NS::Editor::MakeMeshPartsPrototype("wedge45");
    NS::SetActorJsonId(slope, 1);
    NS::Editor::SetActorJsonScale(slope, {8.0f, 8.0f, 8.0f});
    ASSERT_NE(scene.SpawnFromJson(slope), nullptr);
    scene.SetSimulationEnabled(false);
    scene.OnUpdate();

    NS::Editor::StageBuildTool tool;
    tool.Size() = {2.0f, 2.0f, 2.0f};
    const std::optional<NS::Vector3> center =
        tool.Placement(NS::Ray{NS::Vector3{0.0f, 20.0f, 0.0f}, NS::Vector3{0.0f, -1.0f, 0.0f}}, scene.Physics());
    ASSERT_TRUE(center);
    EXPECT_NEAR(center->y, 1.0f, 0.01f);
    EXPECT_NEAR(center->z, -1.0f, 0.01f);
}

TEST(StageBuild, InvalidSizeAndAParallelRayDoNotProduceAPlacement)
{
    NS::Scene scene;
    NS::Editor::StageBuildTool tool;
    EXPECT_FALSE(
        tool.Placement(NS::Ray{NS::Vector3{0.0f, 5.0f, 0.0f}, NS::Vector3{1.0f, 0.0f, 0.0f}}, scene.Physics()));
    tool.Size().x = 0.0f;
    EXPECT_FALSE(
        tool.Placement(NS::Ray{NS::Vector3{0.0f, 5.0f, 0.0f}, NS::Vector3{0.0f, -1.0f, 0.0f}}, scene.Physics()));
}

TEST(StageBuild, RepeatedPlacementUsesSceneHistoryAndRejectsPlayMode)
{
    NS::Scene scene;
    LevelEditorController editor(&scene);
    editor.StageBuilder().SelectPart(NS::Editor::StageBuildTool::Part::Slope);
    editor.SetEditTool(LevelEditorController::EditTool::StageBuild);
    editor.StageBuilder().Size() = {2.5f, 1.5f, 3.5f};
    const NS::Ray ray{NS::Vector3{1.25f, 20.0f, 2.75f}, NS::Vector3{0.0f, -1.0f, 0.0f}};
    const NS::uint32 first = editor.PlaceStagePart(ray);
    const NS::uint32 second = editor.PlaceStagePart(ray);
    ASSERT_NE(first, NS::k_NoActorId);
    ASSERT_NE(second, NS::k_NoActorId);
    EXPECT_NE(first, second);
    EXPECT_EQ(editor.CurrentEditTool(), LevelEditorController::EditTool::StageBuild);
    EXPECT_EQ(editor.Editor().Undo().UndoSize(), 2u);
    EXPECT_TRUE(editor.Editor().HasUnsavedChanges());
    EXPECT_TRUE(editor.Editor().PerformUndo());
    EXPECT_EQ(scene.Actors().FindByActorId(second), nullptr);
    EXPECT_NE(scene.Actors().FindByActorId(first), nullptr);
    EXPECT_TRUE(editor.Editor().PerformRedo());
    EXPECT_NE(scene.Actors().FindByActorId(second), nullptr);

    NS::Scene restored;
    restored.ResetFromJson(scene.ToJson());
    const NS::SceneActor* actor = restored.Actors().FindByActorId(first);
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(actor->ModelClassObj()->MeshRef(), "wedge45");
    EXPECT_FLOAT_EQ(actor->Root().Scale().z, 3.5f);
    EXPECT_FLOAT_EQ(actor->Root().Position().x, 1.25f);

    editor.EnterPlay();
    const std::size_t count = scene.Actors().ActorCount();
    const std::size_t history = editor.Editor().Undo().UndoSize();
    EXPECT_EQ(editor.PlaceStagePart(ray), NS::k_NoActorId);
    EXPECT_EQ(scene.Actors().ActorCount(), count);
    EXPECT_EQ(editor.Editor().Undo().UndoSize(), history);
}

TEST(StageBuild, ViewportInputPlacesOncePerClickAndHonorsUiOwnership)
{
    NS::ApplicationDesc desc;
    desc.window.title = "ステージ作成の入力の試し";
    desc.window.size = {320, 200};
    desc.window.visible = false;
    NS::Application app(desc);
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&app] {
        NS::Scene scene;
        LevelEditorController editor(&scene);
        editor.Setup({});
        editor.SetEditTool(LevelEditorController::EditTool::StageBuild);
        NS::OS::Mouse& mouse = app.Input().Mouse();
        NS::OS::Keyboard& keyboard = app.Input().Keyboard();
        const int originalX = mouse.GetX();
        const int originalY = mouse.GetY();
        mouse.ClearState();
        keyboard.ClearState();
        mouse.OnMove(160, 100);
        editor.SetGameView(0, 0, 320, 200, true);
        editor.SetEditShortcutsBlocked(true);
        mouse.OnButtonDown(NS::OS::MouseButton::Left);
        editor.Tick();
        EXPECT_EQ(scene.Actors().ActorCount(), 0u);
        mouse.OnButtonUp(NS::OS::MouseButton::Left);
        mouse.Update();

        editor.SetEditShortcutsBlocked(false);
        editor.SetGameView(0, 0, 320, 200, false);
        mouse.OnButtonDown(NS::OS::MouseButton::Left);
        editor.Tick();
        EXPECT_EQ(scene.Actors().ActorCount(), 0u);
        mouse.OnButtonUp(NS::OS::MouseButton::Left);
        mouse.Update();

        editor.SetGameView(0, 0, 320, 200, true);
        editor.Tick();
        NS::Gfx::DebugShapes preview;
        editor.DrawSceneViewShapes(preview, NS::Matrix::Identity);
        EXPECT_GT(preview.FaceVertexCount(), 0u);
        mouse.OnButtonDown(NS::OS::MouseButton::Left);
        editor.Tick();
        EXPECT_EQ(scene.Actors().ActorCount(), 1u);
        mouse.Update();
        editor.Tick();
        EXPECT_EQ(scene.Actors().ActorCount(), 1u);
        EXPECT_EQ(editor.CurrentEditTool(), LevelEditorController::EditTool::StageBuild);
        mouse.OnButtonUp(NS::OS::MouseButton::Left);
        mouse.Update();
        mouse.OnButtonDown(NS::OS::MouseButton::Left);
        editor.Tick();
        EXPECT_EQ(scene.Actors().ActorCount(), 2u);
        mouse.OnButtonUp(NS::OS::MouseButton::Left);
        mouse.Update();

        editor.SetEditShortcutsBlocked(true);
        keyboard.OnKeyDown(NS::OS::Key::Escape);
        editor.Tick();
        EXPECT_EQ(editor.CurrentEditTool(), LevelEditorController::EditTool::StageBuild);
        keyboard.OnKeyUp(NS::OS::Key::Escape);
        editor.SetEditShortcutsBlocked(false);
        keyboard.OnKeyDown(NS::OS::Key::Escape);
        editor.Tick();
        EXPECT_EQ(editor.CurrentEditTool(), LevelEditorController::EditTool::Gizmo);

        mouse.OnMove(originalX, originalY);
        mouse.ClearState();
        keyboard.ClearState();
        mouse.Update();
    }));
    EXPECT_EQ(app.Run(), 0);
}
