#include "Editor/EditorActors.h"
#include "Editor/ActionTimelinePanel.h"
#include "Editor/GizmoEditor.h"
#include "Editor/GridMath.h"
#include "Editor/InspectorPanel.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "Editor/PlacementCatalog.h"
#include "Editor/QuitModal.h"
#include "Game/Level/GameScene.h"
#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/GoalActor.h"
#include "NSlib/App/Application.h"
#include "NSlib/App/Layer.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/MaterialFile.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneCamera.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Windows/InputReader.h"
#include "NSlib/Core/Windows/Mouse.h"
#include "NSlib/Core/Windows/StringUtils.h"
#include "Tests/TestActorFile.h"
#include "Tests/TestSceneSetup.h"
#include "Tests/TestCheckLayer.h"
#include "Tests/TestHeadlessImGui.h"
#include "Tests/TestHiddenApp.h"
#include "TestSpawn.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using NS::Editor::LevelEditorController;

namespace
{
    // インスペクタの部品の一覧の行と同じ道で、actor の姿勢の項目 fieldName に value を書く
    template <class Value>
    void WritePoseField(LevelEditorController& editor, NS::SceneActor* actor, const char* fieldName, const Value& value)
    {
        NS_ASSERT(Game, actor != nullptr, "WritePoseField: 渡したアクターか部品が無い");
        const NS::ClassObjInfo* row = nullptr;
        NS::ForEachReflectedClassObj(actor, [&](const NS::ClassObjInfo& candidate, const NS::Object* part) {
            if (part == &actor->TransformObj())
            {
                row = &candidate;
            }
        });
        ASSERT_NE(row, nullptr);
        const NS::FieldDesc* field = NS::FindField(actor->TransformObj().GetReflection(), fieldName);
        ASSERT_NE(field, nullptr);
        ASSERT_EQ(field->set(&actor->TransformObj(), &value), NS::FieldWriteResult::Written);
        editor.NoteFieldEdit(actor, *row, fieldName);
    }

    std::optional<NS::Vector2> FindMoveHandle(const NS::SceneActor* target,
                                              const NS::Matrix& viewProjection,
                                              const NS::Editor::ViewRect& view)
    {
        NS_ASSERT(Game, target != nullptr, "FindMoveHandle: 渡したアクターか部品が無い");
        const NS::AffineDecomposition world = NS::DecomposeAffine(target->TransformObj().WorldMatrix());
        for (int y = 0; y < view.height; ++y)
        {
            for (int x = 0; x < view.width; ++x)
            {
                const NS::Vector2 point{static_cast<float>(x), static_cast<float>(y)};
                if (NS::Editor::GizmoEditor::ToolHandlePick(world.translation,
                                                            world.rotation,
                                                            NS::Editor::GizmoTool::Move,
                                                            point,
                                                            viewProjection,
                                                            NS::Editor::ViewRectSize(view)) !=
                    NS::Editor::GizmoAxis::None)
                {
                    return point;
                }
            }
        }
        return std::nullopt;
    }

    void DragHandle(LevelEditorController& editor, NS::OS::Mouse& mouse, NS::Vector2 handle)
    {
        mouse.OnMove(static_cast<int>(handle.x), static_cast<int>(handle.y));
        mouse.OnButtonDown(NS::OS::MouseButton::Left);
        editor.Tick();
        mouse.Update();
        mouse.OnMove(static_cast<int>(handle.x) + 30, static_cast<int>(handle.y) + 20);
        for (int frame = 0; frame < 4; ++frame)
        {
            editor.Tick();
            mouse.Update();
        }
        mouse.OnButtonUp(NS::OS::MouseButton::Left);
        editor.Tick();
        mouse.Update();
    }

    // 試しの組は RTTI を持たないので、リフレクションの型で確かめてから下の型へ直す
    GL::Level::FollowCameraActor* AsFollowCamera(NS::SceneActor* actor)
    {
        if (actor == nullptr || actor->GetReflection() != GL::Level::FollowCameraActor::StaticReflection())
        {
            return nullptr;
        }
        return static_cast<GL::Level::FollowCameraActor*>(actor);
    }
} // namespace

// Esc は選択を外す。ギズモだけ外すと、次のフレームに残った選択の id から選び直される
TEST(EditorSelection, EscapeClearsTheSelection)
{
    NS::Application app(HiddenAppDesc("選択の試し", {160, 90}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&app] {
        NS::Scene scene;
        LevelEditorController editor(scene);
        const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("GoalActor");
        ASSERT_TRUE(item.has_value());
        editor.PlaceItem(*item);
        ASSERT_NE(editor.SelectedActorId(), NS::k_NoActorId);
        NS::OS::Keyboard& keyboard = app.Input().Keyboard();
        keyboard.ClearState();
        editor.Tick();

        keyboard.OnKeyDown(NS::OS::Key::Escape);
        editor.Tick();
        keyboard.OnKeyUp(NS::OS::Key::Escape);
        editor.Tick();
        EXPECT_EQ(editor.SelectedActorId(), NS::k_NoActorId);
        EXPECT_TRUE(editor.SelectedActorIds().empty());
    }));
    EXPECT_EQ(app.Run(), 0);
}

// プレイ中はシーンに置いたアクターを消さず、ギズモの姿勢の確定でもシーンを未保存にしない。プレイの姿は編集へ戻る組み直しで消える
TEST(EditorSelection, PlayDoesNotEditTheLevel)
{
    NS::Application app(HiddenAppDesc("プレイ中の編集の試し", {160, 90}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([] {
        GL::Level::GameScene scene;
        LevelEditorController editor(scene);
        const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("GoalActor");
        ASSERT_TRUE(item.has_value());
        editor.PlaceItem(*item);
        const NS::uint32 id = editor.SelectedActorId();
        const NS::uint64 versionBefore = editor.Editor().EditVersion();

        editor.EnterPlay();
        editor.DeleteSelectedActor();
        EXPECT_NE(scene.Actors().FindByActorId(id), nullptr);
        editor.BeginTransformEdit();
        WritePoseField(editor, scene.Actors().FindByActorId(id), NS::k_PositionFieldName, NS::Vector3{5.0f, 0.0f, 0.0f});
        editor.CommitTransformEdit();
        EXPECT_EQ(editor.Editor().EditVersion(), versionBefore);
        editor.EnterEdit();
    }));
    EXPECT_EQ(app.Run(), 0);
}

// プレイ中に動かした位置・回転・スケールは、やり直しで戻る先の凍結にも届く
TEST(EditorSelection, PlayTransformEditReachesTheBaseline)
{
    NS::Application app(HiddenAppDesc("プレイ中の変形の試し", {160, 90}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([] {
        GL::Level::GameScene scene;
        LevelEditorController editor(scene);
        const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("GoalActor");
        ASSERT_TRUE(item.has_value());
        editor.PlaceItem(*item);
        const NS::uint32 id = editor.SelectedActorId();

        editor.EnterPlay();
        const NS::Quaternion rotation = NS::Quaternion::CreateFromYawPitchRoll(0.5f, 0.0f, 0.0f);
        NS::SceneActor& actor = *scene.Actors().FindByActorId(id);
        WritePoseField(editor, &actor, NS::k_PositionFieldName, NS::Vector3{5.0f, 1.0f, 2.0f});
        WritePoseField(editor, &actor, NS::k_RotationFieldName, rotation);
        WritePoseField(editor, &actor, NS::k_ScaleFieldName, NS::Vector3{2.0f, 2.0f, 2.0f});

        const nlohmann::json& baseline = editor.PlayBaseline();
        const std::size_t index = NS::FindActorIndexById(baseline, id);
        ASSERT_NE(index, NS::k_NoActorIndex);
        const nlohmann::json& object = NS::SceneJsonActors(baseline)[index];
        EXPECT_FLOAT_EQ(NS::Editor::ActorJsonPosition(object).x, 5.0f);
        EXPECT_FLOAT_EQ(NS::Editor::ActorJsonRotation(object).y, rotation.y);
        EXPECT_FLOAT_EQ(NS::Editor::ActorJsonScale(object).x, 2.0f);
        editor.EnterEdit();
    }));
    EXPECT_EQ(app.Run(), 0);
}

// プレイを始めた時のシーンの JSON は、プレイに入る前に動かしたアクターの位置を持つ
TEST(EditorSelection, PlayBaselineHoldsThePositionBeforeEnterPlay)
{
    NS::Application app(HiddenAppDesc("プレイ開始の JSON の試し", {160, 90}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([] {
        GL::Level::GameScene scene;
        LevelEditorController editor(scene);
        const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("GoalActor");
        ASSERT_TRUE(item.has_value());
        editor.PlaceItem(*item);
        const NS::uint32 id = editor.SelectedActorId();
        NS::SceneActor* prop = scene.Actors().FindByActorId(id);
        ASSERT_NE(prop, nullptr);
        editor.BeginTransformEdit();
        prop->TransformObj().SetPosition(NS::Vector3{3.0f, 0.0f, 7.0f});
        editor.CommitTransformEdit();

        editor.EnterPlay();
        const nlohmann::json& baseline = editor.PlayBaseline();
        const std::size_t index = NS::FindActorIndexById(baseline, id);
        ASSERT_NE(index, NS::k_NoActorIndex);
        const nlohmann::json& object = NS::SceneJsonActors(baseline)[index];
        EXPECT_FLOAT_EQ(NS::Editor::ActorJsonPosition(object).x, 3.0f);
        EXPECT_FLOAT_EQ(NS::Editor::ActorJsonPosition(object).z, 7.0f);
        editor.EnterEdit();
    }));
    EXPECT_EQ(app.Run(), 0);
}

// ギズモで変えた姿勢の項目だけが上書きの一覧に載り、保存したシーンに書かれる
TEST(EditorSelection, GizmoPoseEditBecomesASceneOverride)
{
    nlohmann::json doc = NS::MakeSceneJson();
    AddActorJson(doc, "GoalActor", 1);
    NS::Scene scene;
    scene.ResetFromJson(doc);
    LevelEditorController editor(scene);
    editor.SelectActorById(1);
    NS::SceneActor* prop = scene.Actors().FindByActorId(1);
    ASSERT_NE(prop, nullptr);
    ASSERT_FALSE(prop->IsSceneOverride("Transform", NS::k_ScaleFieldName));

    // ギズモは掴んでいる間に実体の姿勢を直に書き、離した時に CommitTransformEdit で閉じる
    editor.BeginTransformEdit();
    prop->TransformObj().SetScale(NS::Vector3{2.0f, 2.0f, 2.0f});
    editor.CommitTransformEdit();
    EXPECT_TRUE(prop->IsSceneOverride("Transform", NS::k_ScaleFieldName));
    EXPECT_FALSE(prop->IsSceneOverride("Transform", NS::k_RotationFieldName));
    const nlohmann::json saved = scene.ToJson();
    const nlohmann::json& entry = NS::SceneJsonActors(saved)[NS::FindActorIndexById(saved, 1)];
    const nlohmann::json* transform = NS::ClassObjFields(entry, "Transform");
    ASSERT_NE(transform, nullptr);
    EXPECT_FLOAT_EQ(NS::Editor::ActorJsonScale(entry).x, 2.0f);
    EXPECT_FALSE(NS::HasField(*transform, NS::k_RotationFieldName));
}

// プレイ中にインスペクタで変えた欄は、編集へ戻る時に残り、未保存のフラグが立つ
// プレイで動いただけの位置は未保存のフラグに入らない
TEST(EditorSelection, PlayFieldEditIsKeptWhenReturningToEdit)
{
    NS::Application app(HiddenAppDesc("プレイ中の欄の記録の試し", {160, 90}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([] {
        // 置く操作は未保存にするので、読み込んだだけの未保存でないシーンから始める
        nlohmann::json doc = NS::MakeSceneJson();
        AddActorJson(doc, "GoalActor", 1);
        GL::Level::GameScene scene;
        scene.ResetFromJson(doc);
        LevelEditorController editor(scene);
        const NS::uint32 id = 1;
        ASSERT_NE(scene.Actors().FindByActorId(id), nullptr);
        ASSERT_FALSE(editor.Editor().HasUnsavedChanges());

        const auto colorOf = [&scene, id]() {
            const NS::SceneActor* actor = scene.Actors().FindByActorId(id);
            return NS::SerializeClassObjFields(actor->ModelClassObj())["基本色"];
        };
        const nlohmann::json colorBefore = colorOf();
        const NS::Vector3 positionBefore = scene.Actors().FindByActorId(id)->TransformObj().Position();
        NS::Editor::ActionTimelinePanel timeline;

        // 何も変えずに戻る
        editor.EnterPlay();
        EXPECT_FALSE(NS::Editor::HasUnsavedWork(editor, timeline));
        editor.EnterEdit();
        EXPECT_FALSE(editor.Editor().HasUnsavedChanges());

        // 位置だけがプレイで動く
        editor.EnterPlay();
        scene.Actors().FindByActorId(id)->TransformObj().SetPosition(NS::Vector3{7.0f, 3.0f, 1.0f});
        EXPECT_FALSE(NS::Editor::HasUnsavedWork(editor, timeline));
        editor.EnterEdit();
        EXPECT_FALSE(editor.Editor().HasUnsavedChanges());
        EXPECT_EQ(scene.Actors().FindByActorId(id)->TransformObj().Position(), positionBefore);

        // インスペクタの道で色の欄を変える
        editor.EnterPlay();
        NS::SceneActor* actor = scene.Actors().FindByActorId(id);
        ASSERT_NE(actor, nullptr);
        actor->ModelClassObj()->SetColorOverride(NS::Color{0.2f, 0.4f, 0.6f, 1.0f});
        const NS::ClassObjInfo* modelRow = nullptr;
        NS::ForEachReflectedClassObj(actor, [&](const NS::ClassObjInfo& row, const NS::Object* part) {
            if (part == actor->ModelClassObj())
            {
                modelRow = &row;
            }
        });
        ASSERT_NE(modelRow, nullptr);
        editor.NoteFieldEdit(actor, *modelRow, "基本色");
        EXPECT_TRUE(NS::Editor::HasUnsavedWork(editor, timeline));
        EXPECT_FALSE(NS::Editor::SaveAllForQuit(editor, timeline));
        const nlohmann::json colorEdited = colorOf();
        ASSERT_NE(colorEdited, colorBefore);
        editor.EnterEdit();

        EXPECT_EQ(colorOf(), colorEdited);
        EXPECT_TRUE(editor.Editor().HasUnsavedChanges());
    }));
    EXPECT_EQ(app.Run(), 0);
}

// プレイ中に欄を変えてから手で元の値へ戻しても、編集モードと同じく未保存のまま残る
// 初めから上書きにある欄で試す。上書きに無い欄は、変えた時に上書きの一覧へ入るので、戻しても文書が変わる
TEST(EditorSelection, PlayFieldEditRevertedByHandStaysUnsaved)
{
    NS::Application app(HiddenAppDesc("プレイ中に戻した欄の試し", {160, 90}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([] {
        nlohmann::json doc = NS::MakeSceneJson();
        AddActorJson(doc, "GoalActor", 1);
        NS::ActorJsonClassObjs(NS::SceneJsonActors(doc)[0])["Model"]["基本色"] = {0.9f, 0.1f, 0.1f, 1.0f};
        GL::Level::GameScene scene;
        scene.ResetFromJson(doc);
        LevelEditorController editor(scene);
        ASSERT_FALSE(editor.Editor().HasUnsavedChanges());

        editor.EnterPlay();
        NS::SceneActor* actor = scene.Actors().FindByActorId(1);
        ASSERT_NE(actor, nullptr);
        const NS::ClassObjInfo* modelRow = nullptr;
        NS::ForEachReflectedClassObj(actor, [&](const NS::ClassObjInfo& row, const NS::Object* part) {
            if (part == actor->ModelClassObj())
            {
                modelRow = &row;
            }
        });
        ASSERT_NE(modelRow, nullptr);
        ASSERT_TRUE(actor->IsSceneOverride(modelRow->key, "基本色"));
        const std::optional<NS::Color> original = actor->ModelClassObj()->ColorOverride();
        actor->ModelClassObj()->SetColorOverride(NS::Color{0.2f, 0.4f, 0.6f, 1.0f});
        editor.NoteFieldEdit(actor, *modelRow, "基本色");
        actor->ModelClassObj()->SetColorOverride(original);
        editor.NoteFieldEdit(actor, *modelRow, "基本色");
        editor.EnterEdit();

        EXPECT_TRUE(editor.Editor().HasUnsavedChanges());
    }));
    EXPECT_EQ(app.Run(), 0);
}

// ドラッグでマテリアルを付けても色の上書きは変えない。空なら付けたマテリアルの色で描く
TEST(EditorSelection, ApplyingAMaterialKeepsTheColorOverride)
{
    NS::Application app(HiddenAppDesc("マテリアルと色の上書きの試し", {160, 90}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&app] {
        NS::Scene scene;
        scene.SetSimulationEnabled(false);
        LevelEditorController editor(scene);
        const std::string stone = "Assets/Materials/stone.mat";
        const std::string stonePath = NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), stone);
        const NS::Vector3 stoneColor = app.Assets().LoadMaterial(stone)->BaseColor().ToVector3();
        ASSERT_EQ(stoneColor, (NS::Vector3{0.6f, 0.6f, 0.65f}));

        editor.PlaceItem(*NS::Editor::FindPlacementItem(NS::Editor::k_PartsCubeLabel));
        NS::Model* overridden = editor.SelectedActor()->ModelClassObj();
        overridden->SetColorOverride(NS::Color{0.1f, 0.2f, 0.3f});
        ASSERT_TRUE(editor.ApplyMaterialToSelected(stonePath));
        ASSERT_TRUE(overridden->ColorOverride().has_value());
        EXPECT_EQ(overridden->ColorOverride()->ToVector3(), (NS::Vector3{0.1f, 0.2f, 0.3f}));
        EXPECT_EQ(overridden->DrawColor().ToVector3(), (NS::Vector3{0.1f, 0.2f, 0.3f}));

        editor.PlaceItem(*NS::Editor::FindPlacementItem(NS::Editor::k_PartsCubeLabel));
        NS::Model* empty = editor.SelectedActor()->ModelClassObj();
        empty->SetColorOverride(std::nullopt);
        ASSERT_TRUE(editor.ApplyMaterialToSelected(stonePath));
        EXPECT_FALSE(empty->ColorOverride().has_value());
        EXPECT_EQ(empty->DrawColor().ToVector3(), stoneColor);
    }));
    EXPECT_EQ(app.Run(), 0);
}

// マテリアルを付けると未保存になり、書き出して読み直しても付けたマテリアルのまま。同じ物をもう一度付けても版は進まない
TEST(EditorSelection, AppliedMaterialIsSavedAndMarksTheSceneUnsaved)
{
    NS::Application app(HiddenAppDesc("マテリアルの保存の試し", {160, 90}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([] {
        // 置いた立方体を、読み込んだ (未保存でない) シーンとして始める
        NS::Scene source;
        source.SetSimulationEnabled(false);
        NS::uint32 id = NS::k_NoActorId;
        {
            LevelEditorController placing(source);
            placing.PlaceItem(*NS::Editor::FindPlacementItem(NS::Editor::k_PartsCubeLabel));
            id = placing.SelectedActorId();
        }
        ASSERT_NE(id, NS::k_NoActorId);
        NS::Scene scene;
        scene.SetSimulationEnabled(false);
        scene.ResetFromJson(source.ToJson());
        LevelEditorController editor(scene);
        editor.SelectActorById(id);
        ASSERT_FALSE(editor.Editor().HasUnsavedChanges());

        const std::string stone = "Assets/Materials/stone.mat";
        const std::string stonePath = NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), stone);
        ASSERT_TRUE(editor.ApplyMaterialToSelected(stonePath));
        EXPECT_TRUE(editor.Editor().HasUnsavedChanges());

        NS::Scene reloaded;
        reloaded.SetSimulationEnabled(false);
        reloaded.ResetFromJson(scene.ToJson());
        const NS::SceneActor* loaded = reloaded.Actors().FindByActorId(id);
        ASSERT_NE(loaded, nullptr);
        ASSERT_NE(loaded->ModelClassObj(), nullptr);
        EXPECT_EQ(loaded->ModelClassObj()->MaterialRef(), stone);

        const NS::uint64 versionBefore = editor.Editor().EditVersion();
        ASSERT_TRUE(editor.ApplyMaterialToSelected(stonePath));
        EXPECT_EQ(editor.Editor().EditVersion(), versionBefore);
    }));
    EXPECT_EQ(app.Run(), 0);
}

// 追従カメラもギズモで引いて動かせる
TEST(EditorSelection, DraggingTheFollowCameraMovesIt)
{
    NS::Application app(HiddenAppDesc("追従カメラの引きの試し", {320, 200}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&app] {
        NS::Scene scene;
        LevelEditorController editor(scene);
        editor.Setup({});
        const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("FollowCameraActor");
        ASSERT_TRUE(item.has_value());
        editor.PlaceItem(*item);
        const NS::Editor::ViewRect view{.x = 0, .y = 0, .width = 320, .height = 200};
        editor.SetGameView(view.x, view.y, view.width, view.height, true);
        NS::OS::Mouse& mouse = app.Input().Mouse();
        const int startX = mouse.GetX();
        const int startY = mouse.GetY();
        app.Input().Keyboard().ClearState();
        mouse.ClearState();
        editor.Tick();
        editor.FocusSelectedInView();
        editor.Tick();

        const NS::SceneActor* camera = editor.SelectedActor();
        ASSERT_NE(camera, nullptr);
        const std::optional<NS::Vector2> handle =
            FindMoveHandle(camera, scene.GetCameraManager()->ViewProjection(), view);
        ASSERT_TRUE(handle.has_value());

        const NS::Vector3 before = camera->TransformObj().Position();
        const NS::uint64 versionBefore = editor.Editor().EditVersion();
        DragHandle(editor, mouse, *handle);
        EXPECT_NE(editor.SelectedActor()->TransformObj().Position(), before);
        EXPECT_GT(editor.Editor().EditVersion(), versionBefore);

        // 共有の入力を始めの位置と押していない状態へ戻す。残すと後の試しがマウスの動きを読む
        mouse.OnMove(startX, startY);
        mouse.ClearState();
        mouse.Update();
    }));
    EXPECT_EQ(app.Run(), 0);
}

TEST(EditorSelection, DraggingMultipleSelectedActorsMovesEachOnce)
{
    NS::Application app(HiddenAppDesc("複数選択の移動の試し", {320, 200}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&app] {
        NS::Scene scene;
        LevelEditorController editor(scene);
        editor.Setup({});
        const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("GoalActor");
        ASSERT_TRUE(item.has_value());
        editor.PlaceItem(*item);
        const NS::uint32 otherId = editor.SelectedActorId();
        editor.PlaceItem(*item);
        const NS::uint32 actorId = editor.SelectedActorId();
        editor.SelectActors({otherId, actorId}, actorId);
        const NS::Editor::ViewRect view{.x = 0, .y = 0, .width = 320, .height = 200};
        editor.SetGameView(view.x, view.y, view.width, view.height, true);
        NS::OS::Mouse& mouse = app.Input().Mouse();
        const int startX = mouse.GetX();
        const int startY = mouse.GetY();
        app.Input().Keyboard().ClearState();
        mouse.ClearState();
        editor.Tick();
        editor.FocusSelectedInView();
        editor.Tick();

        const auto worldPosition = [&scene](NS::uint32 id) {
            return scene.Actors().FindByActorId(id)->TransformObj().WorldMatrix().Translation();
        };
        const NS::SceneActor* actor = scene.Actors().FindByActorId(actorId);
        ASSERT_NE(actor, nullptr);
        const std::optional<NS::Vector2> handle =
            FindMoveHandle(actor, scene.GetCameraManager()->ViewProjection(), view);
        ASSERT_TRUE(handle.has_value());

        const NS::Vector3 otherBefore = worldPosition(otherId);
        const NS::Vector3 actorBefore = worldPosition(actorId);
        DragHandle(editor, mouse, *handle);
        EXPECT_NE(worldPosition(actorId), actorBefore);
        EXPECT_NE(worldPosition(otherId), otherBefore);
        EXPECT_LT(((worldPosition(otherId) - otherBefore) - (worldPosition(actorId) - actorBefore)).Length(), 0.001f);

        // 共有の入力を始めの位置と押していない状態へ戻す。残すと後の試しがマウスの動きを読む
        mouse.OnMove(startX, startY);
        mouse.ClearState();
        mouse.Update();
    }));
    EXPECT_EQ(app.Run(), 0);
}

TEST(EditorSelection, GizmoMovesAnIndependentOffsetActor)
{
    NS::Application app(HiddenAppDesc("独立した個体のギズモの試し", {320, 200}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&app] {
        NS::Scene scene;
        LevelEditorController editor(scene);
        editor.Setup({});
        const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("GoalActor");
        ASSERT_TRUE(item.has_value());
        editor.PlaceItem(*item);
        const NS::uint32 actorId = editor.SelectedActorId();
        const NS::Vector3 placed = scene.Actors().FindByActorId(actorId)->TransformObj().Position();
        WritePoseField(editor,
                       scene.Actors().FindByActorId(actorId),
                       NS::k_PositionFieldName,
                       NS::Vector3{placed + NS::Vector3{3.0f, 0.0f, 0.0f}});
        const NS::Editor::ViewRect view{.x = 0, .y = 0, .width = 320, .height = 200};
        editor.SetGameView(view.x, view.y, view.width, view.height, true);
        NS::OS::Mouse& mouse = app.Input().Mouse();
        const int startX = mouse.GetX();
        const int startY = mouse.GetY();
        app.Input().Keyboard().ClearState();
        mouse.ClearState();
        editor.Tick();
        editor.FocusSelectedInView();
        editor.Tick();

        const NS::SceneActor* actor = scene.Actors().FindByActorId(actorId);
        ASSERT_NE(actor, nullptr);
        ASSERT_EQ(actor->TransformObj().Position(), actor->TransformObj().WorldMatrix().Translation());
        const std::optional<NS::Vector2> handle =
            FindMoveHandle(actor, scene.GetCameraManager()->ViewProjection(), view);
        ASSERT_TRUE(handle.has_value());

        const NS::Vector3 before = actor->TransformObj().WorldMatrix().Translation();
        DragHandle(editor, mouse, *handle);
        const NS::Vector3 after = scene.Actors().FindByActorId(actorId)->TransformObj().WorldMatrix().Translation();
        EXPECT_NE(after, before);

        // 共有の入力を始めの位置と押していない状態へ戻す。残すと後の試しがマウスの動きを読む
        mouse.OnMove(startX, startY);
        mouse.ClearState();
        mouse.Update();
    }));
    EXPECT_EQ(app.Run(), 0);
}

// 名前を打つ途中で選択を変えても、打った名前は打ち始めた物へ入る。後から選んだ物は改名しない
TEST(EditorSelection, RenameGoesToTheActorBeingTyped)
{
    ImGuiContext* gui = CreateHeadlessImGui(ImVec2{800.0f, 600.0f});
    ImGuiIO& io = ImGui::GetIO();

    NS::Scene scene;
    LevelEditorController editor(scene);
    const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("GoalActor");
    ASSERT_TRUE(item.has_value());
    editor.PlaceItem(*item);
    const NS::uint32 first = editor.SelectedActorId();
    editor.PlaceItem(*item);
    const NS::uint32 second = editor.SelectedActorId();
    ASSERT_NE(first, second);
    const std::string secondName = NS::Editor::ActorDisplayName(scene.Actors().FindByActorId(second));
    editor.SelectActorById(first);

    NS::Editor::InspectorPanel inspector;
    const auto frame = [&] {
        ImGui::NewFrame();
        inspector.Render(editor);
        ImGui::EndFrame();
    };
    frame();
    ImGuiWindow* window = ImGui::FindWindowByName(NS::Editor::k_PanelInspector);
    ASSERT_NE(window, nullptr);
    ImGui::ActivateItemByID(window->GetID("##actorName"));
    frame();
    frame();
    io.AddInputCharactersUTF8("Typed");
    frame();

    editor.SelectActorById(second);
    ImGui::NewFrame();
    ImGui::ClearActiveID();
    inspector.Render(editor);
    ImGui::EndFrame();

    EXPECT_NE(std::string{NS::Editor::ActorDisplayName(scene.Actors().FindByActorId(first))}.find("Typed"),
              std::string::npos);
    EXPECT_EQ(NS::Editor::ActorDisplayName(scene.Actors().FindByActorId(second)), secondName);
    ImGui::DestroyContext(gui);
}

namespace
{
    void RenderInspectorFrame(NS::Editor::InspectorPanel& inspector, LevelEditorController& editor)
    {
        ImGui::NewFrame();
        inspector.Render(editor);
        ImGui::EndFrame();
    }

    // 名前の欄を打ち始めた時に欄が持っていた文字。何も打たずに離すので改名は流れない
    std::string ReadInspectorNameField(NS::Editor::InspectorPanel& inspector, LevelEditorController& editor)
    {
        ImGuiWindow* window = ImGui::FindWindowByName(NS::Editor::k_PanelInspector);
        if (window == nullptr)
        {
            return {};
        }
        const ImGuiID nameId = window->GetID("##actorName");
        ImGui::ActivateItemByID(nameId);
        RenderInspectorFrame(inspector, editor);
        RenderInspectorFrame(inspector, editor);
        std::string text;
        if (ImGuiInputTextState* state = ImGui::GetInputTextState(nameId))
        {
            text = state->TextToRevertTo.Data;
        }
        ImGui::NewFrame();
        ImGui::ClearActiveID();
        inspector.Render(editor);
        ImGui::EndFrame();
        return text;
    }
} // namespace

// 一覧の道で改名すると、インスペクタの名前の欄は実体の今の名前を出す
TEST(EditorSelection, InspectorNameFieldFollowsRename)
{
    ImGuiContext* gui = CreateHeadlessImGui(ImVec2{800.0f, 600.0f});

    NS::Scene scene;
    LevelEditorController editor(scene);
    const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("GoalActor");
    ASSERT_TRUE(item.has_value());
    editor.PlaceItem(*item);
    const NS::uint32 id = editor.SelectedActorId();
    const std::string before = NS::Editor::ActorDisplayName(scene.Actors().FindByActorId(id));

    NS::Editor::InspectorPanel inspector;
    RenderInspectorFrame(inspector, editor);
    ASSERT_EQ(ReadInspectorNameField(inspector, editor), before);

    editor.RenameActor(id, "Renamed");
    RenderInspectorFrame(inspector, editor);
    EXPECT_EQ(ReadInspectorNameField(inspector, editor), "Renamed");
    ImGui::DestroyContext(gui);
}

// 参照先の候補は置いた物の並びのまま、その並びの番号で並ぶ
TEST(EditorSelection, ReferenceOptionsFollowThePlacedActors)
{
    NS::Scene scene;
    (void)SpawnFromActorFile<GL::Level::GoalActor>(scene, "GoalActor", "a");
    (void)SpawnFromActorFile<GL::Level::GoalActor>(scene, "GoalActor", "b");
    (void)SpawnFromActorFile<GL::Level::GoalActor>(scene, "GoalActor", "c");

    const std::vector<NS::SceneActor*> placed = NS::Editor::PlacedActors(scene.Actors());
    ASSERT_EQ(placed.size(), 3u);
    const std::vector<NS::Editor::ActorRefOption> options = NS::Editor::MakeActorRefOptions(scene.Actors());
    ASSERT_EQ(options.size(), placed.size());
    for (std::size_t i = 0; i < placed.size(); ++i)
    {
        EXPECT_EQ(options[i].id, placed[i]->Id());
        EXPECT_EQ(options[i].label.rfind("[" + std::to_string(i) + "] ", 0), 0u) << options[i].label;
    }
}

TEST(EditorSelection, InspectorAxisInputsWriteOnlyThatAxis)
{
    ImGuiContext* gui = CreateHeadlessImGui(ImVec2{800.0f, 700.0f}, HeadlessImGuiInput::Instant);
    ImGuiIO& io = ImGui::GetIO();

    NS::Scene scene;
    LevelEditorController editor(scene);
    editor.PlaceItem(*NS::Editor::FindPlacementItem("GoalActor"));
    const NS::uint32 id = editor.SelectedActorId();
    const NS::Vector3 beforePosition = editor.SelectedActor()->TransformObj().Position();
    const NS::Vector3 beforeScale = editor.SelectedActor()->TransformObj().Scale();
    NS::Editor::InspectorPanel inspector;
    const auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
        ImGui::SetNextWindowSize(ImVec2{472.0f, 650.0f});
        inspector.Render(editor);
        ImGui::EndFrame();
    };
    frame();
    frame();
    const NS::uint64 versionBefore = editor.Editor().EditVersion();
    // 姿勢は部品の一覧の Transform の行で描く。部品は鍵で、欄の表は名前で、欄は並びの番号で ID を積む
    const char* rows[] = {NS::k_PositionFieldName, NS::k_RotationFieldName, NS::k_ScaleFieldName};
    const char* values[] = {"12.5", "30", "2.5"};
    const NS::ReflectionInfo* transformInfo = NS::Transform::StaticReflection();
    for (int row = 0; row < 3; ++row)
    {
        int fieldIndex = -1;
        for (std::size_t i = 0; i < transformInfo->fieldCount; ++i)
        {
            if (std::string_view{rows[row]} == transformInfo->fields[i].name)
            {
                fieldIndex = static_cast<int>(i);
            }
        }
        ImGuiWindow* window = ImGui::FindWindowByName(NS::Editor::k_PanelInspector);
        ImGuiID inputId = ImHashStr("Transform", 0, window->ID);
        inputId = ImHashStr("##fields", 0, inputId);
        inputId = ImHashData(&fieldIndex, sizeof(fieldIndex), inputId);
        inputId = ImHashStr("Y", 0, inputId);
        inputId = ImHashStr("##value", 0, inputId);
        ImGui::ActivateItemByID(inputId);
        GImGui->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
        frame();
        EXPECT_EQ(GImGui->ActiveId, inputId);
        io.AddInputCharactersUTF8(values[row]);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
    }
    const NS::Transform& edited = scene.Actors().FindByActorId(id)->TransformObj();
    EXPECT_GT(editor.Editor().EditVersion(), versionBefore);
    EXPECT_FLOAT_EQ(edited.Position().x, beforePosition.x);
    EXPECT_FLOAT_EQ(edited.Position().y, 12.5f);
    EXPECT_FLOAT_EQ(edited.Position().z, beforePosition.z);
    EXPECT_NEAR(NS::QuaternionToEulerDegrees(edited.Rotation()).y, 30.0f, 0.001f);
    EXPECT_FLOAT_EQ(edited.Scale().x, beforeScale.x);
    EXPECT_FLOAT_EQ(edited.Scale().y, 2.5f);
    EXPECT_FLOAT_EQ(edited.Scale().z, beforeScale.z);
    ImGui::DestroyContext(gui);
}

TEST(EditorSelection, AssetAndActorSelectionsExcludeEachOther)
{
    NS::Scene scene;
    LevelEditorController editor(scene);
    const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("GoalActor");
    ASSERT_TRUE(item.has_value());
    editor.PlaceItem(*item);
    const NS::uint32 placed = editor.SelectedActorId();
    ASSERT_NE(placed, NS::k_NoActorId);

    editor.SelectAsset(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets/Materials/stone.mat"));
    EXPECT_EQ(editor.SelectedActorId(), NS::k_NoActorId);
    EXPECT_TRUE(editor.SelectedActorIds().empty());
    EXPECT_EQ(editor.SelectedAssetPath(), "Assets/Materials/stone.mat");
    EXPECT_NE(NS::Cast<NS::Material>(editor.SelectedAsset()), nullptr);

    editor.SelectActorById(placed);
    EXPECT_TRUE(editor.SelectedAssetPath().empty());
    EXPECT_EQ(editor.SelectedAsset(), nullptr);
}

// 素材の値を変えた後は、どの道で素材の選択が外れても .mat に新しい値が書かれている
TEST(EditorSelection, ClearingTheAssetSelectionWritesTheEditedMaterial)
{
    NS::Application app(HiddenAppDesc("素材の選択を外す試し", {320, 200}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&app] {
        // 選ぶ素材は ContentRoot の下に限られるので、Assets の下に試しのフォルダを作り、終わったら消す
        const std::string folder =
            "EditorAssetSelectTest" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const std::string directory = NS::OS::FileSystem::Combine(
            NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), folder);
        ASSERT_TRUE(NS::OS::FileSystem::CreateDirectories(directory));
        const std::string stone =
            NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets/Materials/stone.mat");
        const std::optional<std::string> stoneText = NS::OS::FileSystem::ReadAllText(stone);
        ASSERT_TRUE(stoneText.has_value());

        NS::Scene scene;
        LevelEditorController editor(scene);
        editor.Setup({});
        const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("GoalActor");
        ASSERT_TRUE(item.has_value());
        editor.PlaceItem(*item);
        const NS::uint32 actorId = editor.SelectedActorId();
        ASSERT_NE(actorId, NS::k_NoActorId);
        const NS::Editor::ViewRect view{.x = 0, .y = 0, .width = 320, .height = 200};
        editor.SetGameView(view.x, view.y, view.width, view.height, true);
        NS::OS::Mouse& mouse = app.Input().Mouse();
        const int startX = mouse.GetX();
        const int startY = mouse.GetY();
        app.Input().Keyboard().ClearState();
        mouse.ClearState();
        editor.Tick();
        editor.FocusSelectedInView();
        editor.Tick();

        struct ClearPath
        {
            const char* name;
            std::function<void()> clear;
        };
        const std::vector<ClearPath> paths{
            {"アクターの選択", [&editor, actorId] { editor.SelectActorById(actorId); }},
            {"ギズモの選択",
             [&editor, &mouse, &view] {
                 // 選んだアクターは画面の中心に居るので、中心のクリックでギズモが拾う
                 mouse.OnMove(view.width / 2, view.height / 2);
                 mouse.OnButtonDown(NS::OS::MouseButton::Left);
                 editor.Tick();
                 mouse.Update();
                 mouse.OnButtonUp(NS::OS::MouseButton::Left);
                 editor.Tick();
                 mouse.Update();
                 EXPECT_NE(editor.SelectedActorId(), NS::k_NoActorId);
             }},
            {"置く", [&editor, item] { editor.PlaceItem(*item); }},
            {"シーン設定", [&editor] { editor.SelectSceneSettings(); }},
            {"別の素材", [&editor, &stone] { editor.SelectAsset(stone); }},
        };
        std::size_t index = 0;
        for (const ClearPath& path : paths)
        {
            ++index;
            const std::string fileName = "edit_" + std::to_string(index) + ".mat";
            const std::string file = NS::OS::FileSystem::Combine(directory, fileName);
            ASSERT_TRUE(NS::OS::FileSystem::WriteAllBytes(
                file, std::as_bytes(std::span<const char>{stoneText->data(), stoneText->size()})));
            editor.SelectAsset(file);
            ASSERT_EQ(editor.SelectedAssetPath(), "Assets/" + folder + "/" + fileName) << path.name;
            NS::Material* material = NS::Cast<NS::Material>(editor.SelectedAsset());
            ASSERT_NE(material, nullptr) << path.name;
            const NS::FieldDesc* blend = NS::FindField(material->GetReflection(), "混ぜ方");
            ASSERT_NE(blend, nullptr);
            const std::string additive = "Additive";
            ASSERT_EQ(blend->set(material, &additive), NS::FieldWriteResult::Written);
            editor.MarkAssetEdited();

            path.clear();

            EXPECT_NE(editor.SelectedAssetPath(), "Assets/" + folder + "/" + fileName) << path.name;
            const std::optional<std::string> text = NS::OS::FileSystem::ReadAllText(file);
            ASSERT_TRUE(text.has_value()) << path.name;
            NS::MaterialFileDesc written{};
            std::string error;
            ASSERT_TRUE(NS::ParseMaterialJson(*text, written, error)) << path.name << error;
            EXPECT_EQ(written.blend, NS::Gfx::BlendMode::Additive) << path.name;
        }

        std::error_code error;
        std::filesystem::remove_all(std::filesystem::path{NS::OS::StringUtils::WideFromUtf8(directory)}, error);
        // 共有の入力を始めの位置と押していない状態へ戻す。残すと後の試しがマウスの動きを読む
        mouse.OnMove(startX, startY);
        mouse.ClearState();
        mouse.Update();
    }));
    EXPECT_EQ(app.Run(), 0);
}

// 置く・筆で空いた升に置く・複製 は、どれも作って未保存にする
// 複製の参照は一緒に写した物を指す。作った物を選ぶかは道ごとに決まり、筆は選ばない。プレイ中はどの道も作らない
TEST(EditorCreate, EveryCreatePathMarksTheSceneUnsavedAndPlayCreatesNothing)
{
    NS::Application app(HiddenAppDesc("作る道の試し", {160, 90}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([] {
        GL::Level::GameScene scene;
        LevelEditorController editor(scene);
        editor.Setup({});
        const std::optional<NS::Editor::PlacementItem> prop = NS::Editor::FindPlacementItem("GoalActor");
        const std::optional<NS::Editor::PlacementItem> camera = NS::Editor::FindPlacementItem("FollowCameraActor");
        ASSERT_TRUE(prop.has_value());
        ASSERT_TRUE(camera.has_value());

        // 置く: 作った物を選ぶ
        std::size_t objectsBefore = scene.Actors().ActorCount();
        editor.PlaceItem(*prop);
        const NS::uint32 placed = editor.SelectedActorId();
        EXPECT_NE(scene.Actors().FindByActorId(placed), nullptr);
        EXPECT_EQ(scene.Actors().ActorCount(), objectsBefore + 1);
        EXPECT_TRUE(editor.Editor().HasUnsavedChanges());

        const NS::uint32 kept = placed;

        // 複製: 写した物どうしの参照は写した先を指し、写した物を選ぶ
        editor.PlaceItem(*camera);
        const NS::uint32 follower = editor.SelectedActorId();
        GL::Level::FollowCameraActor* followActor = AsFollowCamera(scene.Actors().FindByActorId(follower));
        ASSERT_NE(followActor, nullptr);
        ASSERT_EQ(NS::ApplyJsonFields(&followActor->Vcam(), nlohmann::json{{"追従対象", {{"ref", kept}}}}), 0u);
        editor.SelectActors({kept, follower}, follower);
        objectsBefore = scene.Actors().ActorCount();
        editor.DuplicateSelectedActor();
        EXPECT_EQ(scene.Actors().ActorCount(), objectsBefore + 2);
        EXPECT_TRUE(editor.Editor().HasUnsavedChanges());
        const std::vector<NS::uint32> copies = editor.SelectedActorIds();
        ASSERT_EQ(copies.size(), 2u);
        EXPECT_NE(copies[0], kept);
        EXPECT_NE(copies[1], follower);
        const GL::Level::FollowCameraActor* followCopy = AsFollowCamera(scene.Actors().FindByActorId(copies[1]));
        ASSERT_NE(followCopy, nullptr);
        EXPECT_EQ(followCopy->Vcam().TargetRef().id, copies[0]);

        // プレイ中: どの道も作らない
        editor.SelectActors({kept}, kept);
        editor.EnterPlay();
        objectsBefore = scene.Actors().ActorCount();
        editor.PlaceItem(*prop);
        editor.DuplicateSelectedActor();
        EXPECT_EQ(scene.Actors().ActorCount(), objectsBefore);
        editor.EnterEdit();
    }));
    EXPECT_EQ(app.Run(), 0);
}

// プレイ中は、シーンを変える道をどれも断る。シーンも .actor のファイルも変わらない
// インスペクタで欄を触る道は断らない。シーンのアクターへ書く欄はプレイ開始の写しへ写し、シーンで上書きできない欄は .actor へ書く
// (.actor へ書く道は ActorFile.PlayEditOfAnUnmarkedFieldGoesToTheActorFileAndSurvivesReturningToEdit が見張る)
TEST(EditorSelection, PlayEditsNeitherTheSceneNorTheActorFiles)
{
    NS::Application app(HiddenAppDesc("プレイ中に断る試し", {160, 90}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([] {
        // 置く地形の部品の .actor を試しの置き場へ写す。プレイ中の道がどれも .actor のファイルを書かない事を中身で比べる
        const ScopedActorFileDirectory actorFiles("EditorPlayEdit");
        const std::optional<std::string> partsText = NS::OS::FileSystem::ReadAllText(
            NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets/Actors/MapPartsActor.actor"));
        ASSERT_TRUE(partsText.has_value());
        WriteActorFile(actorFiles.Path(), "MapPartsActor", *partsText);
        NS::ActorFileLibrary::Get().Reload();
        const auto actorFileTexts = [&actorFiles] {
            std::vector<std::string> texts;
            for (const std::string& path : NS::OS::FileSystem::ListFiles(actorFiles.Path(), ".actor"))
            {
                texts.push_back(path + "\n" + NS::OS::FileSystem::ReadAllText(path).value_or(""));
            }
            return texts;
        };
        // 開くレベルは Assets の下に限られるので、試しのフォルダを作り、終わったら消す
        const std::string folder =
            "EditorPlayEditTest" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const std::string directory = NS::OS::FileSystem::Combine(
            NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), folder);
        ASSERT_TRUE(NS::OS::FileSystem::CreateDirectories(directory));
        const std::string levelText = NS::MakeSceneJson().dump();
        ASSERT_TRUE(NS::OS::FileSystem::WriteAllBytes(
            NS::OS::FileSystem::Combine(directory, "other.scene"),
            std::as_bytes(std::span<const char>{levelText.data(), levelText.size()})));

        GL::Level::GameScene scene;
        LevelEditorController editor(scene);
        editor.Setup({});
        const std::optional<NS::Editor::PlacementItem> cube =
            NS::Editor::FindPlacementItem(NS::Editor::k_PartsCubeLabel);
        ASSERT_TRUE(cube.has_value());
        editor.PlaceItem(*cube);
        const NS::uint32 followerId = editor.SelectedActorId();
        editor.PlaceItem(*cube);
        const NS::uint32 sourceId = editor.SelectedActorId();
        editor.SelectActorById(sourceId);

        editor.EnterPlay();
        ASSERT_NE(scene.Actors().FindByActorId(sourceId), nullptr);
        ASSERT_NE(scene.Actors().FindByActorId(followerId), nullptr);
        const std::string stone =
            NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets/Materials/stone.mat");

        struct EditPath
        {
            const char* name;
            std::function<void()> edit;
        };
        const std::vector<EditPath> paths{
            {"マテリアルを付ける", [&editor, &stone] { EXPECT_FALSE(editor.ApplyMaterialToSelected(stone)); }},
            {"置く", [&editor, &cube] { editor.PlaceItem(*cube); }},
            {"複製", [&editor] { editor.DuplicateSelectedActor(); }},
            {"消す", [&editor] { editor.DeleteSelectedActor(); }},
            {"改名", [&editor, sourceId] { editor.RenameActor(sourceId, "改名した"); }},
            {"空の設定", [&editor] { EXPECT_FALSE(editor.SetSkyboxPath("Assets/Skybox/none.png")); }},
            {"レベルを開く",
             [&editor, &folder] { EXPECT_FALSE(editor.Editor().OpenLevel(folder + "/other")); }},
        };
        for (const EditPath& path : paths)
        {
            const nlohmann::json sceneBefore = scene.ToJson();
            const std::vector<std::string> actorFilesBefore = actorFileTexts();

            path.edit();

            EXPECT_EQ(scene.ToJson(), sceneBefore) << path.name;
            EXPECT_EQ(actorFileTexts(), actorFilesBefore) << path.name;
        }
        editor.EnterEdit();

        std::error_code error;
        std::filesystem::remove_all(std::filesystem::path{NS::OS::StringUtils::WideFromUtf8(directory)}, error);
    }));
    EXPECT_EQ(app.Run(), 0);
}

// ギズモの道具のキーは、渡された止めるかの答えだけで止まる。偽なら効き、真なら効かない
// コントローラーの更新が答えを見る事は LevelOpenTest.CtrlSOverwritesTheOpenLevel が Ctrl+S で見る
TEST(EditorSelection, EditShortcutsFollowTheBlockedAnswer)
{
    NS::Application app(HiddenAppDesc("ショートカットを止める試し", {320, 200}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&app] {
        const NS::Editor::ViewRect view{.x = 0, .y = 0, .width = 320, .height = 200};
        NS::OS::Keyboard& keyboard = app.Input().Keyboard();
        NS::OS::Mouse& mouse = app.Input().Mouse();
        const int startX = mouse.GetX();
        const int startY = mouse.GetY();
        keyboard.ClearState();
        mouse.ClearState();

        // ギズモの道具のキーは、答えを Tick で受けるギズモを直に回して見る
        NS::ActorList actors;
        NS::Editor::GizmoEditor gizmo;
        gizmo.SetInput(&app.Input());
        gizmo.SetActive(true);
        ASSERT_NE(gizmo.Tool(), NS::Editor::GizmoTool::Rotate);

        for (const bool blocked : {true, false})
        {
            const NS::Editor::GizmoTool toolBefore = gizmo.Tool();
            // 前のキーをギズモへ渡さないよう、E の直前から読む
            NS::OS::InputReader gizmoReader;
            keyboard.OnKeyDown(NS::OS::Key::E);
            gizmo.Tick(NS::Matrix::Identity, view, true, blocked, gizmoReader.Read(), actors);
            keyboard.OnKeyUp(NS::OS::Key::E);
            NS::Editor::GizmoTool expectedTool = NS::Editor::GizmoTool::Rotate;
            if (blocked)
            {
                expectedTool = toolBefore;
            }
            EXPECT_EQ(gizmo.Tool(), expectedTool) << "ギズモの道具 止める=" << blocked;
        }

        // 共有の入力を始めの位置と押していない状態へ戻す。残すと後の試しがマウスの動きを読む
        mouse.OnMove(startX, startY);
        mouse.ClearState();
        mouse.Update();
        keyboard.ClearState();
    }));
    EXPECT_EQ(app.Run(), 0);
}

namespace
{
    // GoalActor を count 体置き、置いた順の永続 id を返す
    std::vector<NS::uint32> PlaceGoals(LevelEditorController& editor, int count)
    {
        const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("GoalActor");
        std::vector<NS::uint32> ids;
        EXPECT_TRUE(item.has_value()) << "置く物の一覧に GoalActor が無い";
        if (!item.has_value())
        {
            return ids;
        }
        for (int i = 0; i < count; ++i)
        {
            editor.PlaceItem(*item);
            ids.push_back(editor.SelectedActorId());
        }
        return ids;
    }

    void SetParent(NS::Scene& scene, NS::uint32 child, NS::uint32 parent)
    {
        scene.Actors().FindByActorId(child)->TransformObj().SetParent(NS::ActorRef{parent});
    }
} // namespace

// 親の欄はファイルに相手の名前で書かれ、読み直すと同じ相手の id を指す
TEST(EditorParent, ParentFieldSurvivesSaveAndLoadByName)
{
    NS::Application app(HiddenAppDesc("親の保存の試し", {160, 90}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([] {
        NS::Scene scene;
        LevelEditorController editor(scene);
        const std::vector<NS::uint32> ids = PlaceGoals(editor, 2);
        SetParent(scene, ids[1], ids[0]);
        const std::string parentName = scene.Actors().FindByActorId(ids[0])->Name();

        const std::string text = NS::SerializeSceneToJson(scene.ToJson());
        const nlohmann::json file = nlohmann::json::parse(text);
        const std::size_t childIndex = NS::FindActorIndexById(file, ids[1]);
        ASSERT_LT(childIndex, NS::SceneJsonActors(file).size());
        const nlohmann::json* transform = NS::ClassObjFields(NS::SceneJsonActors(file)[childIndex], "Transform");
        ASSERT_NE(transform, nullptr);
        ASSERT_TRUE(transform->contains(NS::k_ParentFieldName)) << text;
        EXPECT_EQ(transform->at(NS::k_ParentFieldName).at("ref"), parentName);

        nlohmann::json loaded;
        ASSERT_TRUE(NS::DeserializeSceneFromJson(loaded, text));
        scene.ResetFromJson(loaded);
        const NS::SceneActor* child = scene.Actors().FindByActorId(ids[1]);
        ASSERT_NE(child, nullptr);
        EXPECT_EQ(child->TransformObj().Parent().id, ids[0]);
    }));
    EXPECT_EQ(app.Run(), 0);
}

// 親の無いアクターは親の欄を書かない。親の欄の無い今のステージは、読んで書き直しても親の欄が増えない
TEST(EditorParent, EmptyParentIsNotWritten)
{
    nlohmann::json document;
    const std::optional<std::string> path = NS::BuildLevelPath("Scenes/stages/apricot_ruins");
    ASSERT_TRUE(path.has_value());
    ASSERT_TRUE(NS::LoadSceneFromJsonFile(document, *path));

    NS::Application app(HiddenAppDesc("空の親の試し", {160, 90}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&] {
        NS::Scene scene;
        scene.ResetFromJson(document);
        const std::string after = NS::SerializeSceneToJson(scene.ToJson());
        EXPECT_EQ(after.find(std::string{"\""} + NS::k_ParentFieldName + "\""), std::string::npos);
    }));
    EXPECT_EQ(app.Run(), 0);
}

// 子孫は根から順に各 1 回だけ返す。親子が輪になっていても止まる
TEST(EditorParent, CollectsDescendantsOnceEvenInACycle)
{
    NS::Application app(HiddenAppDesc("子孫の試し", {160, 90}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([] {
        NS::Scene scene;
        LevelEditorController editor(scene);
        const std::vector<NS::uint32> ids = PlaceGoals(editor, 4);
        SetParent(scene, ids[1], ids[0]);
        SetParent(scene, ids[2], ids[1]);

        const std::vector<NS::uint32> fromRoot = NS::Editor::CollectWithDescendants(scene.Actors(), {ids[0]});
        EXPECT_EQ(fromRoot, (std::vector<NS::uint32>{ids[0], ids[1], ids[2]}));
        const std::vector<NS::uint32> parentAndChild =
            NS::Editor::CollectWithDescendants(scene.Actors(), {ids[1], ids[0]});
        EXPECT_EQ(parentAndChild, (std::vector<NS::uint32>{ids[1], ids[0], ids[2]}));

        SetParent(scene, ids[0], ids[2]);
        const std::vector<NS::uint32> inCycle = NS::Editor::CollectWithDescendants(scene.Actors(), {ids[0]});
        EXPECT_EQ(inCycle, (std::vector<NS::uint32>{ids[0], ids[1], ids[2]}));
    }));
    EXPECT_EQ(app.Run(), 0);
}

// 自分自身と自分の子孫は親にできない。親を外すのはいつでもできる
TEST(EditorParent, CannotParentToSelfOrDescendant)
{
    NS::Application app(HiddenAppDesc("付け替えの可否の試し", {160, 90}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([] {
        NS::Scene scene;
        LevelEditorController editor(scene);
        const std::vector<NS::uint32> ids = PlaceGoals(editor, 3);
        SetParent(scene, ids[1], ids[0]);
        SetParent(scene, ids[2], ids[1]);

        EXPECT_FALSE(NS::Editor::CanReparent(scene.Actors(), ids[0], ids[0]));
        EXPECT_FALSE(NS::Editor::CanReparent(scene.Actors(), ids[0], ids[2]));
        EXPECT_TRUE(NS::Editor::CanReparent(scene.Actors(), ids[2], ids[0]));
        EXPECT_TRUE(NS::Editor::CanReparent(scene.Actors(), ids[1], NS::k_NoActorId));
    }));
    EXPECT_EQ(app.Run(), 0);
}

namespace
{
    NS::Vector3 WorldPosition(NS::Scene& scene, NS::uint32 id)
    {
        return scene.Actors().FindByActorId(id)->TransformObj().WorldMatrix().Translation();
    }

    // 主対象 primary を選び直し、ギズモの移動の取っ手を 1 回ドラッグする。選ぶ物は selection
    void DragPrimary(NS::Application& app,
                     NS::Scene& scene,
                     LevelEditorController& editor,
                     std::vector<NS::uint32> selection,
                     NS::uint32 primary)
    {
        editor.SelectActors(std::move(selection), primary);
        const NS::Editor::ViewRect view{.x = 0, .y = 0, .width = 320, .height = 200};
        editor.SetGameView(view.x, view.y, view.width, view.height, true);
        NS::OS::Mouse& mouse = app.Input().Mouse();
        const int startX = mouse.GetX();
        const int startY = mouse.GetY();
        app.Input().Keyboard().ClearState();
        mouse.ClearState();
        editor.Tick();
        editor.FocusSelectedInView();
        editor.Tick();
        const std::optional<NS::Vector2> handle = FindMoveHandle(
            scene.Actors().FindByActorId(primary), scene.GetCameraManager()->ViewProjection(), view);
        ASSERT_TRUE(handle.has_value());
        DragHandle(editor, mouse, *handle);
        // 共有の入力を始めの位置と押していない状態へ戻す。残すと後の試しがマウスの動きを読む
        mouse.OnMove(startX, startY);
        mouse.ClearState();
        mouse.Update();
    }
} // namespace

// 親を 1 体だけ選んでギズモで動かすと、子と孫が同じだけ動く
TEST(EditorParent, GizmoMovesDescendantsWithTheParent)
{
    NS::Application app(HiddenAppDesc("親のギズモの試し", {320, 200}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&app] {
        NS::Scene scene;
        LevelEditorController editor(scene);
        editor.Setup({});
        const std::vector<NS::uint32> ids = PlaceGoals(editor, 3);
        SetParent(scene, ids[1], ids[0]);
        SetParent(scene, ids[2], ids[1]);
        const NS::Vector3 parentBefore = WorldPosition(scene, ids[0]);
        const NS::Vector3 childBefore = WorldPosition(scene, ids[1]);
        const NS::Vector3 grandchildBefore = WorldPosition(scene, ids[2]);

        DragPrimary(app, scene, editor, {ids[0]}, ids[0]);
        const NS::Vector3 moved = WorldPosition(scene, ids[0]) - parentBefore;
        EXPECT_GT(moved.Length(), 0.001f);
        EXPECT_LT(((WorldPosition(scene, ids[1]) - childBefore) - moved).Length(), 0.001f);
        EXPECT_LT(((WorldPosition(scene, ids[2]) - grandchildBefore) - moved).Length(), 0.001f);
    }));
    EXPECT_EQ(app.Run(), 0);
}

// 親と子を両方選んで動かしても、子は 1 回分だけ動く
TEST(EditorParent, SelectingParentAndChildMovesTheChildOnce)
{
    NS::Application app(HiddenAppDesc("親と子を選んだギズモの試し", {320, 200}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&app] {
        NS::Scene scene;
        LevelEditorController editor(scene);
        editor.Setup({});
        const std::vector<NS::uint32> ids = PlaceGoals(editor, 2);
        SetParent(scene, ids[1], ids[0]);
        const NS::Vector3 parentBefore = WorldPosition(scene, ids[0]);
        const NS::Vector3 childBefore = WorldPosition(scene, ids[1]);

        DragPrimary(app, scene, editor, {ids[1], ids[0]}, ids[0]);
        const NS::Vector3 moved = WorldPosition(scene, ids[0]) - parentBefore;
        EXPECT_GT(moved.Length(), 0.001f);
        EXPECT_LT(((WorldPosition(scene, ids[1]) - childBefore) - moved).Length(), 0.001f);
    }));
    EXPECT_EQ(app.Run(), 0);
}

// 付け替えは世界座標を変えず、保存して読み直しても残る。自分の子孫を親にする付け替えと、プレイ中の付け替えは断る
TEST(EditorParent, ReparentKeepsWorldAndSurvivesSaving)
{
    NS::Application app(HiddenAppDesc("付け替えの試し", {160, 90}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([] {
        GL::Level::GameScene scene;
        LevelEditorController editor(scene);
        const std::vector<NS::uint32> ids = PlaceGoals(editor, 3);
        const NS::Vector3 childBefore = WorldPosition(scene, ids[1]);

        EXPECT_TRUE(editor.ReparentActor(ids[1], ids[0]));
        EXPECT_EQ(scene.Actors().FindByActorId(ids[1])->TransformObj().Parent().id, ids[0]);
        EXPECT_LT((WorldPosition(scene, ids[1]) - childBefore).Length(), 0.001f);

        // 自分の子孫を親にする付け替えは断り、親を変えない
        EXPECT_FALSE(editor.ReparentActor(ids[0], ids[1]));
        EXPECT_EQ(scene.Actors().FindByActorId(ids[0])->TransformObj().Parent().id, NS::k_NoActorId);

        nlohmann::json loaded;
        ASSERT_TRUE(NS::DeserializeSceneFromJson(loaded, NS::SerializeSceneToJson(scene.ToJson())));
        const std::size_t index = NS::FindActorIndexById(loaded, ids[1]);
        ASSERT_LT(index, NS::SceneJsonActors(loaded).size());
        const nlohmann::json* transform = NS::ClassObjFields(NS::SceneJsonActors(loaded)[index], "Transform");
        ASSERT_NE(transform, nullptr);
        EXPECT_EQ(NS::FieldActorRef(*transform, NS::k_ParentFieldName).id, ids[0]);

        editor.EnterPlay();
        EXPECT_FALSE(editor.ReparentActor(ids[2], ids[0]));
        EXPECT_EQ(scene.Actors().FindByActorId(ids[2])->TransformObj().Parent().id, NS::k_NoActorId);
        editor.EnterEdit();
    }));
    EXPECT_EQ(app.Run(), 0);
}

// インスペクターで親の位置を書くと、子が同じだけ動く。ほかに選んだ物は動かない
TEST(EditorParent, InspectorPoseEditMovesDescendants)
{
    NS::Application app(HiddenAppDesc("インスペクターの親の試し", {160, 90}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([] {
        NS::Scene scene;
        LevelEditorController editor(scene);
        const std::vector<NS::uint32> ids = PlaceGoals(editor, 3);
        SetParent(scene, ids[1], ids[0]);
        editor.SelectActors({ids[2], ids[0]}, ids[0]);
        const NS::Vector3 parentBefore = WorldPosition(scene, ids[0]);
        const NS::Vector3 childBefore = WorldPosition(scene, ids[1]);
        const NS::Vector3 otherBefore = WorldPosition(scene, ids[2]);
        const NS::Vector3 offset{0.0f, 2.0f, 0.0f};

        editor.BeginClassObjEdit();
        NS::SceneActor* parent = scene.Actors().FindByActorId(ids[0]);
        WritePoseField(editor, parent, NS::k_PositionFieldName, NS::Vector3{parentBefore + offset});
        editor.FinishFieldWrite(parent, NS::FieldType::Vector3);
        editor.CommitClassObjEdit();

        EXPECT_LT((WorldPosition(scene, ids[1]) - (childBefore + offset)).Length(), 0.001f);
        EXPECT_LT((WorldPosition(scene, ids[2]) - otherBefore).Length(), 0.001f);
    }));
    EXPECT_EQ(app.Run(), 0);
}

// 1 回の編集の中で親を動かしてから元の位置へ戻すと、子も元の位置へ戻る
TEST(EditorParent, ParentBackAtStartPutsChildrenBack)
{
    NS::Application app(HiddenAppDesc("親を戻す試し", {160, 90}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([] {
        NS::Scene scene;
        LevelEditorController editor(scene);
        const std::vector<NS::uint32> ids = PlaceGoals(editor, 2);
        SetParent(scene, ids[1], ids[0]);
        editor.SelectActorById(ids[0]);
        const NS::Vector3 parentBefore = scene.Actors().FindByActorId(ids[0])->TransformObj().Position();
        const NS::Vector3 childBefore = scene.Actors().FindByActorId(ids[1])->TransformObj().Position();

        editor.BeginClassObjEdit();
        NS::SceneActor* parent = scene.Actors().FindByActorId(ids[0]);
        WritePoseField(editor, parent, NS::k_PositionFieldName, NS::Vector3{parentBefore + NS::Vector3{9.0f, 0.0f, 0.0f}});
        editor.FinishFieldWrite(parent, NS::FieldType::Vector3);
        WritePoseField(editor, parent, NS::k_PositionFieldName, parentBefore);
        editor.FinishFieldWrite(parent, NS::FieldType::Vector3);
        editor.CommitClassObjEdit();

        EXPECT_EQ(scene.Actors().FindByActorId(ids[1])->TransformObj().Position(), childBefore);
    }));
    EXPECT_EQ(app.Run(), 0);
}

// 改名・ギズモで動かす・親の付け替え は、どれも読み込んだだけのシーンを未保存にする
// 複製は EditorCreate、シーン設定は EditorInspector.SkyboxEditMarksTheSceneUnsaved、
// プレイから戻る時は EditorSelection.PlayFieldEditIsKeptWhenReturningToEdit が見る
TEST(EditorUnsaved, RenameGizmoAndReparentMarkTheSceneUnsaved)
{
    nlohmann::json doc = NS::MakeSceneJson();
    AddActorJson(doc, "GoalActor", 1);
    AddActorJson(doc, "GoalActor", 2);
    struct Edit
    {
        const char* name;
        std::function<void(LevelEditorController&, NS::Scene&)> run;
    };
    const std::vector<Edit> edits{
        {"改名", [](LevelEditorController& editor, NS::Scene&) { editor.RenameActor(1, "改名した"); }},
        {"ギズモ",
         [](LevelEditorController& editor, NS::Scene& scene) {
             editor.SelectActorById(1);
             editor.BeginTransformEdit();
             scene.Actors().FindByActorId(1)->TransformObj().SetPosition(NS::Vector3{4.0f, 0.0f, 0.0f});
             editor.CommitTransformEdit();
         }},
        {"親の付け替え",
         [](LevelEditorController& editor, NS::Scene&) { EXPECT_TRUE(editor.ReparentActor(2, 1)); }},
    };
    for (const Edit& edit : edits)
    {
        NS::Scene scene;
        scene.ResetFromJson(doc);
        LevelEditorController editor(scene);
        ASSERT_FALSE(editor.Editor().HasUnsavedChanges()) << edit.name;
        edit.run(editor, scene);
        EXPECT_TRUE(editor.Editor().HasUnsavedChanges()) << edit.name;
    }
}
