#include "Editor/EditorActors.h"
#include "Editor/GizmoEditor.h"
#include "Editor/GridMath.h"
#include "Editor/InspectorPanel.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "Editor/PlacementCatalog.h"
#include "Editor/Undo/ActorSnapshotApplier.h"
#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/PropActor.h"
#include "NSlib/App/Application.h"
#include "NSlib/App/Layer.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/StringUtils.h"
#include "Tests/TestActorFile.h"
#include "Tests/TestCheckLayer.h"

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

namespace
{
    // インスペクタの部品の一覧の行と同じ道で、actor の姿勢の項目 fieldName に value を書く
    template <class Value>
    void WritePoseField(LevelEditorController& editor, NS::SceneActor* actor, const char* fieldName, const Value& value)
    {
        NS_ASSERT(Game, actor != nullptr, "WritePoseField: 渡したアクターか部品が無い");
        const NS::ClassObjInfo* row = nullptr;
        NS::ForEachReflectedClassObj(actor, [&](const NS::ClassObjInfo& candidate, const NS::Object* part) {
            if (part == &actor->Root())
            {
                row = &candidate;
            }
        });
        ASSERT_NE(row, nullptr);
        const NS::FieldDesc* field = NS::FindField(actor->Root().GetReflection(), fieldName);
        ASSERT_NE(field, nullptr);
        field->set(&actor->Root(), &value);
        editor.NoteFieldEdit(actor, *row, fieldName);
    }

    std::optional<NS::Vector2> FindMoveHandle(const NS::SceneActor* target,
                                              const NS::Matrix& viewProjection,
                                              const NS::Editor::ViewRect& view)
    {
        NS_ASSERT(Game, target != nullptr, "FindMoveHandle: 渡したアクターか部品が無い");
        const NS::AffineDecomposition world = NS::DecomposeAffine(target->Root().WorldMatrix());
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
    NS::ApplicationDesc desc;
    desc.window.title = "選択の試し";
    desc.window.size = {160, 90};
    desc.window.visible = false;
    NS::Application app(desc);
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&app] {
        NS::Scene scene;
        LevelEditorController editor(&scene);
        const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("PropActor");
        ASSERT_TRUE(item.has_value());
        editor.PlaceItem(*item);
        ASSERT_NE(editor.SelectedActorId(), NS::k_NoActorId);
        NS::OS::Keyboard& keyboard = app.Input().Keyboard();
        keyboard.ClearState();
        keyboard.Update();
        editor.Tick();

        keyboard.OnKeyDown(NS::OS::Key::Escape);
        editor.Tick();
        keyboard.OnKeyUp(NS::OS::Key::Escape);
        keyboard.Update();
        editor.Tick();
        EXPECT_EQ(editor.SelectedActorId(), NS::k_NoActorId);
        EXPECT_TRUE(editor.SelectedActorIds().empty());
    }));
    EXPECT_EQ(app.Run(), 0);
}

// プレイ中は配置物を消さず、履歴も積まない。編集へ戻る組み直しで消え、履歴だけがプレイの姿を指して残る
TEST(EditorSelection, PlayDoesNotEditTheLevel)
{
    NS::ApplicationDesc desc;
    desc.window.title = "プレイ中の編集の試し";
    desc.window.size = {160, 90};
    desc.window.visible = false;
    NS::Application app(desc);
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([] {
        NS::Scene scene;
        LevelEditorController editor(&scene);
        const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("PropActor");
        ASSERT_TRUE(item.has_value());
        editor.PlaceItem(*item);
        const NS::uint32 id = editor.SelectedActorId();
        const std::size_t undoBefore = editor.Editor().Undo().UndoSize();

        editor.EnterPlay();
        editor.DeleteSelectedActor();
        EXPECT_NE(scene.Actors().FindByActorId(id), nullptr);
        editor.BeginTransformEdit();
        WritePoseField(editor, scene.Actors().FindByActorId(id), NS::k_PositionFieldName, NS::Vector3{5.0f, 0.0f, 0.0f});
        editor.CommitTransformEdit();
        EXPECT_EQ(editor.Editor().Undo().UndoSize(), undoBefore);
        editor.EnterEdit();
    }));
    EXPECT_EQ(app.Run(), 0);
}

// プレイ中に動かした位置・回転・スケールは、やり直しで戻る先の凍結にも届く
TEST(EditorSelection, PlayTransformEditReachesTheBaseline)
{
    NS::ApplicationDesc desc;
    desc.window.title = "プレイ中の変形の試し";
    desc.window.size = {160, 90};
    desc.window.visible = false;
    NS::Application app(desc);
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([] {
        NS::Scene scene;
        LevelEditorController editor(&scene);
        const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("PropActor");
        ASSERT_TRUE(item.has_value());
        editor.PlaceItem(*item);
        const NS::uint32 id = editor.SelectedActorId();

        editor.EnterPlay();
        const NS::Quaternion rotation = NS::Quaternion::CreateFromYawPitchRoll(0.5f, 0.0f, 0.0f);
        NS::SceneActor& actor = *scene.Actors().FindByActorId(id);
        WritePoseField(editor, &actor, NS::k_PositionFieldName, NS::Vector3{5.0f, 1.0f, 2.0f});
        WritePoseField(editor, &actor, NS::k_RotationFieldName, rotation);
        WritePoseField(editor, &actor, NS::k_ScaleFieldName, NS::Vector3{2.0f, 2.0f, 2.0f});

        const nlohmann::json& baseline = scene.PlayBaseline();
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

// ギズモで変えた姿勢の項目だけが上書きの一覧に載り、保存したシーンに書かれる。取り消すと一覧からも外れる
TEST(EditorSelection, GizmoPoseEditBecomesASceneOverride)
{
    nlohmann::json object = NS::MakeActorJson();
    NS::SetActorJsonFile(object, "PropActor");
    NS::SetActorJsonId(object, 1);
    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonActors(doc).push_back(std::move(object));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    LevelEditorController editor(&scene);
    NS::Editor::ActorSnapshotApplier applier(&scene);
    editor.Editor().SetApplier(&applier);
    editor.SelectActorById(1);
    NS::SceneActor* prop = scene.Actors().FindByActorId(1);
    ASSERT_NE(prop, nullptr);
    ASSERT_FALSE(prop->IsSceneOverride("Transform", NS::k_ScaleFieldName));

    // ギズモは掴んでいる間に実体の姿勢を直に書き、離した時に CommitTransformEdit で閉じる
    editor.BeginTransformEdit();
    prop->Root().SetScale(NS::Vector3{2.0f, 2.0f, 2.0f});
    editor.CommitTransformEdit();
    EXPECT_TRUE(prop->IsSceneOverride("Transform", NS::k_ScaleFieldName));
    EXPECT_FALSE(prop->IsSceneOverride("Transform", NS::k_RotationFieldName));
    const nlohmann::json saved = scene.ToJson();
    const nlohmann::json& entry = NS::SceneJsonActors(saved)[NS::FindActorIndexById(saved, 1)];
    const nlohmann::json* transform = NS::ClassObjFields(entry, "Transform");
    ASSERT_NE(transform, nullptr);
    EXPECT_FLOAT_EQ(NS::Editor::ActorJsonScale(entry).x, 2.0f);
    EXPECT_FALSE(NS::HasField(*transform, NS::k_RotationFieldName));

    ASSERT_TRUE(editor.Editor().PerformUndo());
    prop = scene.Actors().FindByActorId(1);
    ASSERT_NE(prop, nullptr);
    EXPECT_EQ(prop->Root().Scale(), NS::Vector3::One);
    EXPECT_FALSE(prop->IsSceneOverride("Transform", NS::k_ScaleFieldName));
}

// プレイ中にインスペクタで変えた欄は、編集へ戻る時に 1 まとまりの履歴になり、未保存の印が付く
// プレイで動いただけの位置は履歴にも未保存の印にも入らない
TEST(EditorSelection, PlayFieldEditIsRecordedWhenReturningToEdit)
{
    NS::ApplicationDesc desc;
    desc.window.title = "プレイ中の欄の記録の試し";
    desc.window.size = {160, 90};
    desc.window.visible = false;
    NS::Application app(desc);
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([] {
        NS::Scene scene;
        LevelEditorController editor(&scene);
        NS::Editor::ActorSnapshotApplier applier(&scene);
        editor.Editor().SetApplier(&applier);
        const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("PropActor");
        ASSERT_TRUE(item.has_value());
        editor.PlaceItem(*item);
        const NS::uint32 id = editor.SelectedActorId();
        editor.Editor().Undo().Clear();
        ASSERT_FALSE(editor.Editor().HasUnsavedChanges());

        const auto colorOf = [&scene, id]() {
            const NS::SceneActor* actor = scene.Actors().FindByActorId(id);
            return NS::SerializeClassObjFields(actor->ModelClassObj())["基本色"];
        };
        const nlohmann::json colorBefore = colorOf();
        const NS::Vector3 positionBefore = scene.Actors().FindByActorId(id)->Root().Position();

        // 何も変えずに戻る
        editor.EnterPlay();
        editor.EnterEdit();
        EXPECT_FALSE(editor.Editor().HasUnsavedChanges());

        // 位置だけがプレイで動く
        editor.EnterPlay();
        scene.Actors().FindByActorId(id)->Root().SetPosition(NS::Vector3{7.0f, 3.0f, 1.0f});
        editor.EnterEdit();
        EXPECT_FALSE(editor.Editor().HasUnsavedChanges());
        EXPECT_EQ(scene.Actors().FindByActorId(id)->Root().Position(), positionBefore);

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
        const nlohmann::json colorEdited = colorOf();
        ASSERT_NE(colorEdited, colorBefore);
        editor.EnterEdit();

        EXPECT_EQ(colorOf(), colorEdited);
        EXPECT_TRUE(editor.Editor().HasUnsavedChanges());
        EXPECT_EQ(editor.Editor().Undo().UndoSize(), 1u);
        EXPECT_TRUE(editor.Editor().PerformUndo());
        EXPECT_EQ(colorOf(), colorBefore);
    }));
    EXPECT_EQ(app.Run(), 0);
}

// ドラッグでマテリアルを付けても色の上書きは変えない。空なら付けたマテリアルの色で描く
TEST(EditorSelection, ApplyingAMaterialKeepsTheColorOverride)
{
    NS::ApplicationDesc desc;
    desc.window.title = "マテリアルと色の上書きの試し";
    desc.window.size = {160, 90};
    desc.window.visible = false;
    NS::Application app(desc);
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&app] {
        NS::Scene scene;
        scene.SetSimulationEnabled(false);
        LevelEditorController editor(&scene);
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

// 追従カメラをギズモで引いた分も undo に積む。動かすのは初期姿勢の欄だが、控えは部品の値まで写す
TEST(EditorSelection, DraggingTheFollowCameraIsUndoable)
{
    NS::ApplicationDesc desc;
    desc.window.title = "追従カメラの引きの試し";
    desc.window.size = {320, 200};
    desc.window.visible = false;
    NS::Application app(desc);
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&app] {
        NS::Scene scene;
        LevelEditorController editor(&scene);
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

        const std::size_t undoBefore = editor.Editor().Undo().UndoSize();
        const NS::Vector3 before = camera->Root().Position();
        DragHandle(editor, mouse, *handle);
        EXPECT_NE(editor.SelectedActor()->Root().Position(), before);
        EXPECT_EQ(editor.Editor().Undo().UndoSize(), undoBefore + 1);

        // 共有の入力を始めの位置と押していない状態へ戻す。残すと後の試しがマウスの動きを読む
        mouse.OnMove(startX, startY);
        mouse.ClearState();
        mouse.Update();
    }));
    EXPECT_EQ(app.Run(), 0);
}

TEST(EditorSelection, DraggingMultipleSelectedActorsMovesEachOnce)
{
    NS::ApplicationDesc desc;
    desc.window.title = "複数選択の移動の試し";
    desc.window.size = {320, 200};
    desc.window.visible = false;
    NS::Application app(desc);
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&app] {
        NS::Scene scene;
        LevelEditorController editor(&scene);
        editor.Setup({});
        const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("PropActor");
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
            return scene.Actors().FindByActorId(id)->Root().WorldMatrix().Translation();
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
    NS::ApplicationDesc desc;
    desc.window.title = "独立した個体のギズモの試し";
    desc.window.size = {320, 200};
    desc.window.visible = false;
    NS::Application app(desc);
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&app] {
        NS::Scene scene;
        LevelEditorController editor(&scene);
        editor.Setup({});
        const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("PropActor");
        ASSERT_TRUE(item.has_value());
        editor.PlaceItem(*item);
        const NS::uint32 actorId = editor.SelectedActorId();
        const NS::Vector3 placed = scene.Actors().FindByActorId(actorId)->Root().Position();
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
        ASSERT_EQ(actor->Root().Position(), actor->Root().WorldMatrix().Translation());
        const std::optional<NS::Vector2> handle =
            FindMoveHandle(actor, scene.GetCameraManager()->ViewProjection(), view);
        ASSERT_TRUE(handle.has_value());

        const NS::Vector3 before = actor->Root().WorldMatrix().Translation();
        DragHandle(editor, mouse, *handle);
        const NS::Vector3 after = scene.Actors().FindByActorId(actorId)->Root().WorldMatrix().Translation();
        EXPECT_NE(after, before);

        // 共有の入力を始めの位置と押していない状態へ戻す。残すと後の試しがマウスの動きを読む
        mouse.OnMove(startX, startY);
        mouse.ClearState();
        mouse.Update();
    }));
    EXPECT_EQ(app.Run(), 0);
}

// 地形の部品でない物が居る升には筆で重ねて置かない
TEST(EditorBrush, DoesNotStackOnAnOccupiedCell)
{
    NS::ApplicationDesc desc;
    desc.window.title = "筆の重なりの試し";
    desc.window.size = {160, 90};
    desc.window.visible = false;
    NS::Application app(desc);
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([] {
        NS::Scene scene;
        LevelEditorController editor(&scene);
        editor.Setup({});
        editor.Editor().Palette().SetActiveSlot(2);
        ASSERT_STREQ(editor.Editor().Palette().CurrentTemplateName(), "ゴール");
        editor.Editor().PlaceUnderCursorProgrammatic(0, 0, 0);
        const std::size_t placed = scene.Actors().ActorCount();
        editor.Editor().PlaceUnderCursorProgrammatic(0, 0, 0);
        EXPECT_EQ(scene.Actors().ActorCount(), placed);
    }));
    EXPECT_EQ(app.Run(), 0);
}

// R は今の向きに縦軸の四半回転を足す。傾けた物も傾きを保つ
TEST(EditorBrush, RotateKeepsTheTilt)
{
    NS::ApplicationDesc desc;
    desc.window.title = "筆の回転の試し";
    desc.window.size = {160, 90};
    desc.window.visible = false;
    NS::Application app(desc);
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([] {
        NS::Scene scene;
        LevelEditorController editor(&scene);
        editor.Setup({});
        editor.Editor().Palette().SetActiveSlot(0);
        editor.Editor().PlaceUnderCursorProgrammatic(0, 0, 0);
        ASSERT_EQ(scene.Actors().ActorCount(), 1u);
        const NS::uint32 id = scene.Actors().ActorAt(0)->Id();
        editor.SelectActorById(id);
        WritePoseField(
            editor, scene.Actors().FindByActorId(id), NS::k_RotationFieldName, NS::Quaternion::CreateFromYawPitchRoll(0.0f, 0.5f, 0.0f));
        const auto up = [&scene, id] {
            return NS::Vector3::Transform(NS::Vector3::UnitY, scene.Actors().FindByActorId(id)->Root().Rotation());
        };
        const float tiltBefore = up().y;

        editor.Editor().RotateAtProgrammatic(0, 0, 0);
        EXPECT_NEAR(up().y, tiltBefore, 1.0e-4f);
    }));
    EXPECT_EQ(app.Run(), 0);
}

// 名前を打つ途中で選択を変えても、打った名前は打ち始めた物へ入る。後から選んだ物は改名しない
TEST(EditorSelection, RenameGoesToTheActorBeingTyped)
{
    ImGuiContext* gui = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2{800.0f, 600.0f};
    io.DeltaTime = 1.0f / 60.0f;
    NS::uint8* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    NS::Scene scene;
    LevelEditorController editor(&scene);
    const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("PropActor");
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

// 一覧の道で改名しても、Undo で名前が戻っても、インスペクタの名前の欄は実体の今の名前を出す
TEST(EditorSelection, InspectorNameFieldFollowsRenameAndUndo)
{
    ImGuiContext* gui = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2{800.0f, 600.0f};
    io.DeltaTime = 1.0f / 60.0f;
    NS::uint8* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    NS::Scene scene;
    LevelEditorController editor(&scene);
    const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("PropActor");
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

    ASSERT_TRUE(editor.Editor().PerformUndo());
    RenderInspectorFrame(inspector, editor);
    EXPECT_EQ(ReadInspectorNameField(inspector, editor), before);
    ImGui::DestroyContext(gui);
}

// 参照先の候補は置いた物の並びのまま、その並びの番号で並ぶ
TEST(EditorSelection, ReferenceOptionsFollowThePlacedActors)
{
    NS::Scene scene;
    (void)scene.SpawnActor<GL::Level::PropActor>("a");
    (void)scene.SpawnActor<GL::Level::PropActor>("b");
    (void)scene.SpawnActor<GL::Level::PropActor>("c");

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

TEST(EditorSelection, InspectorAxisInputsCommitSeparateUndoableEdits)
{
    ImGuiContext* gui = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2{800.0f, 700.0f};
    io.DeltaTime = 1.0f / 60.0f;
    io.ConfigInputTrickleEventQueue = false;
    NS::uint8* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    NS::Scene scene;
    LevelEditorController editor(&scene);
    NS::Editor::ActorSnapshotApplier applier(&scene);
    editor.Editor().SetApplier(&applier);
    editor.PlaceItem(*NS::Editor::FindPlacementItem("PropActor"));
    const NS::uint32 id = editor.SelectedActorId();
    const NS::Vector3 beforePosition = editor.SelectedActor()->Root().Position();
    const NS::Vector3 beforeScale = editor.SelectedActor()->Root().Scale();
    const NS::Quaternion beforeRotation = editor.SelectedActor()->Root().Rotation();
    const std::size_t beforeUndo = editor.Editor().Undo().UndoSize();
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
        EXPECT_EQ(editor.Editor().Undo().UndoSize(), beforeUndo + row + 1);
    }
    const NS::Transform& edited = scene.Actors().FindByActorId(id)->Root();
    EXPECT_FLOAT_EQ(edited.Position().x, beforePosition.x);
    EXPECT_FLOAT_EQ(edited.Position().y, 12.5f);
    EXPECT_FLOAT_EQ(edited.Position().z, beforePosition.z);
    EXPECT_NEAR(NS::QuaternionToEulerDegrees(edited.Rotation()).y, 30.0f, 0.001f);
    EXPECT_FLOAT_EQ(edited.Scale().x, beforeScale.x);
    EXPECT_FLOAT_EQ(edited.Scale().y, 2.5f);
    EXPECT_FLOAT_EQ(edited.Scale().z, beforeScale.z);
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_TRUE(editor.Editor().PerformUndo());
    }
    const NS::Transform& restored = scene.Actors().FindByActorId(id)->Root();
    EXPECT_EQ(restored.Position(), beforePosition);
    EXPECT_EQ(restored.Rotation(), beforeRotation);
    EXPECT_EQ(restored.Scale(), beforeScale);
    ImGui::DestroyContext(gui);
}

TEST(EditorSelection, AssetAndActorSelectionsExcludeEachOther)
{
    NS::Scene scene;
    LevelEditorController editor(&scene);
    const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("PropActor");
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
    NS::ApplicationDesc desc;
    desc.window.title = "素材の選択を外す試し";
    desc.window.size = {320, 200};
    desc.window.visible = false;
    NS::Application app(desc);
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
        LevelEditorController editor(&scene);
        editor.Setup({});
        const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("PropActor");
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
            blend->set(material, &additive);
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

// 置く・筆で空いた升に置く・複製 は、どれも 1 回の履歴で作って未保存にし、Undo 1 回で消える
// 複製の参照は一緒に写した物を指す。作った物を選ぶかは道ごとに決まり、筆は選ばない。プレイ中はどの道も作らない
TEST(EditorCreate, EveryCreatePathIsOneUndoableEditAndPlayCreatesNothing)
{
    NS::ApplicationDesc desc;
    desc.window.title = "作る道の試し";
    desc.window.size = {160, 90};
    desc.window.visible = false;
    NS::Application app(desc);
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([] {
        NS::Scene scene;
        LevelEditorController editor(&scene);
        editor.Setup({});
        const std::optional<NS::Editor::PlacementItem> prop = NS::Editor::FindPlacementItem("PropActor");
        const std::optional<NS::Editor::PlacementItem> camera = NS::Editor::FindPlacementItem("FollowCameraActor");
        ASSERT_TRUE(prop.has_value());
        ASSERT_TRUE(camera.has_value());
        const NS::Editor::UndoStack& undo = editor.Editor().Undo();

        // 置く: 作った物を選ぶ
        std::size_t objectsBefore = scene.Actors().ActorCount();
        std::size_t undoBefore = undo.UndoSize();
        editor.PlaceItem(*prop);
        const NS::uint32 placed = editor.SelectedActorId();
        EXPECT_NE(scene.Actors().FindByActorId(placed), nullptr);
        EXPECT_EQ(scene.Actors().ActorCount(), objectsBefore + 1);
        EXPECT_EQ(undo.UndoSize(), undoBefore + 1);
        EXPECT_TRUE(editor.Editor().HasUnsavedChanges());
        EXPECT_TRUE(editor.Editor().PerformUndo());
        EXPECT_EQ(scene.Actors().FindByActorId(placed), nullptr);
        EXPECT_EQ(scene.Actors().ActorCount(), objectsBefore);

        // 筆で空いた升に置く: 今の選択を変えない
        editor.PlaceItem(*prop);
        const NS::uint32 kept = editor.SelectedActorId();
        editor.Editor().Palette().SetActiveSlot(0);
        objectsBefore = scene.Actors().ActorCount();
        undoBefore = undo.UndoSize();
        editor.Editor().PlaceUnderCursorProgrammatic(10, 0, 10);
        EXPECT_NE(NS::Editor::FindActorIdAtCell(scene.Actors(), 10, 0, 10), NS::k_NoActorId);
        EXPECT_EQ(scene.Actors().ActorCount(), objectsBefore + 1);
        EXPECT_EQ(undo.UndoSize(), undoBefore + 1);
        EXPECT_TRUE(editor.Editor().HasUnsavedChanges());
        EXPECT_EQ(editor.SelectedActorId(), kept);
        EXPECT_TRUE(editor.Editor().PerformUndo());
        EXPECT_EQ(NS::Editor::FindActorIdAtCell(scene.Actors(), 10, 0, 10), NS::k_NoActorId);
        EXPECT_EQ(scene.Actors().ActorCount(), objectsBefore);

        // 複製: 写した物どうしの参照は写した先を指し、写した物を選ぶ
        editor.PlaceItem(*camera);
        const NS::uint32 follower = editor.SelectedActorId();
        GL::Level::FollowCameraActor* followActor = AsFollowCamera(scene.Actors().FindByActorId(follower));
        ASSERT_NE(followActor, nullptr);
        ASSERT_EQ(NS::ApplyJsonFields(&followActor->Vcam(), nlohmann::json{{"追従対象", {{"ref", kept}}}}), 0u);
        editor.SelectActors({kept, follower}, follower);
        objectsBefore = scene.Actors().ActorCount();
        undoBefore = undo.UndoSize();
        editor.DuplicateSelectedActor();
        EXPECT_EQ(scene.Actors().ActorCount(), objectsBefore + 2);
        EXPECT_EQ(undo.UndoSize(), undoBefore + 1);
        EXPECT_TRUE(editor.Editor().HasUnsavedChanges());
        const std::vector<NS::uint32> copies = editor.SelectedActorIds();
        ASSERT_EQ(copies.size(), 2u);
        EXPECT_NE(copies[0], kept);
        EXPECT_NE(copies[1], follower);
        const GL::Level::FollowCameraActor* followCopy = AsFollowCamera(scene.Actors().FindByActorId(copies[1]));
        ASSERT_NE(followCopy, nullptr);
        EXPECT_EQ(followCopy->Vcam().TargetRef().id, copies[0]);
        EXPECT_TRUE(editor.Editor().PerformUndo());
        EXPECT_EQ(scene.Actors().FindByActorId(copies[0]), nullptr);
        EXPECT_EQ(scene.Actors().FindByActorId(copies[1]), nullptr);
        EXPECT_EQ(scene.Actors().ActorCount(), objectsBefore);

        // プレイ中: どの道も作らず、履歴も積まない
        editor.SelectActors({kept}, kept);
        editor.EnterPlay();
        objectsBefore = scene.Actors().ActorCount();
        undoBefore = undo.UndoSize();
        editor.PlaceItem(*prop);
        editor.DuplicateSelectedActor();
        editor.Editor().PlaceUnderCursorProgrammatic(12, 0, 12);
        EXPECT_EQ(scene.Actors().ActorCount(), objectsBefore);
        EXPECT_EQ(undo.UndoSize(), undoBefore);
        editor.EnterEdit();
    }));
    EXPECT_EQ(app.Run(), 0);
}

// プレイ中は、シーンを変える道をどれも断る。シーンも履歴も .actor のファイルも変わらない
// インスペクタで欄を触る道は断らない。配置物へ書く欄はプレイ開始の写しへ写し、印の無い欄は .actor へ書く
// (.actor へ書く道は ActorFile.PlayEditOfAnUnmarkedFieldGoesToTheActorFileAndSurvivesReturningToEdit が見張る)
TEST(EditorSelection, PlayEditsNeitherTheSceneNorTheActorFiles)
{
    NS::ApplicationDesc desc;
    desc.window.title = "プレイ中に断る試し";
    desc.window.size = {160, 90};
    desc.window.visible = false;
    NS::Application app(desc);
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

        NS::Scene scene;
        LevelEditorController editor(&scene);
        editor.Setup({});
        const std::optional<NS::Editor::PlacementItem> cube =
            NS::Editor::FindPlacementItem(NS::Editor::k_PartsCubeLabel);
        ASSERT_TRUE(cube.has_value());
        editor.PlaceItem(*cube);
        const NS::uint32 followerId = editor.SelectedActorId();
        editor.PlaceItem(*cube);
        const NS::uint32 sourceId = editor.SelectedActorId();
        editor.Editor().Palette().SetActiveSlot(0);
        editor.Editor().PlaceUnderCursorProgrammatic(10, 0, 10);
        ASSERT_NE(NS::Editor::FindActorIdAtCell(scene.Actors(), 10, 0, 10), NS::k_NoActorId);
        // やり直しの履歴も 1 つ持たせる
        editor.PlaceItem(*cube);
        ASSERT_TRUE(editor.Editor().PerformUndo());
        ASSERT_TRUE(editor.Editor().CanRedo());
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
            {"筆で置き換える", [&editor] { editor.Editor().PlaceUnderCursorProgrammatic(10, 0, 10); }},
            {"筆で回す", [&editor] { editor.Editor().RotateAtProgrammatic(10, 0, 10); }},
            {"筆で消す", [&editor] { editor.Editor().DeleteAtProgrammatic(10, 0, 10); }},
            {"元に戻す", [&editor] { EXPECT_FALSE(editor.Editor().PerformUndo()); }},
            {"やり直す", [&editor] { EXPECT_FALSE(editor.Editor().PerformRedo()); }},
            {"レベルを開く",
             [&editor, &folder] { EXPECT_FALSE(editor.Editor().OpenLevel(folder + "/other")); }},
        };
        const NS::Editor::UndoStack& undo = editor.Editor().Undo();
        for (const EditPath& path : paths)
        {
            const nlohmann::json sceneBefore = scene.ToJson();
            const std::size_t undoBefore = undo.UndoSize();
            const std::size_t redoBefore = undo.RedoSize();
            const std::vector<std::string> actorFilesBefore = actorFileTexts();

            path.edit();

            EXPECT_EQ(scene.ToJson(), sceneBefore) << path.name;
            EXPECT_EQ(undo.UndoSize(), undoBefore) << path.name;
            EXPECT_EQ(undo.RedoSize(), redoBefore) << path.name;
            EXPECT_EQ(actorFileTexts(), actorFilesBefore) << path.name;
        }
        editor.EnterEdit();

        std::error_code error;
        std::filesystem::remove_all(std::filesystem::path{NS::OS::StringUtils::WideFromUtf8(directory)}, error);
    }));
    EXPECT_EQ(app.Run(), 0);
}

// 編集のショートカットは、渡された止めるかの答えだけで止まる。
// 偽なら Ctrl+Z・R・番号・ギズモの道具のキーが効き、真ならどれも効かない
TEST(EditorSelection, EditShortcutsFollowTheBlockedAnswer)
{
    NS::ApplicationDesc desc;
    desc.window.title = "ショートカットを止める試し";
    desc.window.size = {320, 200};
    desc.window.visible = false;
    NS::Application app(desc);
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&app] {
        NS::Scene scene;
        LevelEditorController editor(&scene);
        editor.Setup({});
        editor.Editor().Palette().SetActiveSlot(0);
        editor.Editor().PlaceUnderCursorProgrammatic(0, 0, 0);
        ASSERT_EQ(scene.Actors().ActorCount(), 1u);
        editor.SelectActorById(scene.Actors().ActorAt(0)->Id());
        const NS::Editor::ViewRect view{.x = 0, .y = 0, .width = 320, .height = 200};
        editor.SetGameView(view.x, view.y, view.width, view.height, true);
        NS::OS::Keyboard& keyboard = app.Input().Keyboard();
        NS::OS::Mouse& mouse = app.Input().Mouse();
        const int startX = mouse.GetX();
        const int startY = mouse.GetY();
        keyboard.ClearState();
        keyboard.Update();
        mouse.ClearState();
        editor.Tick();
        editor.FocusSelectedInView();
        editor.Tick();
        // 置いた物は画面の中心に居るので、中心のカーソルで筆の R がその物を回す
        mouse.OnMove(view.width / 2, view.height / 2);
        mouse.Update();
        editor.SetGizmoToolActive(false);

        const auto press = [&keyboard, &editor](NS::OS::Key key, bool ctrl) {
            if (ctrl)
            {
                keyboard.OnKeyDown(NS::OS::Key::Ctrl);
            }
            keyboard.OnKeyDown(key);
            editor.Tick();
            keyboard.OnKeyUp(key);
            keyboard.OnKeyUp(NS::OS::Key::Ctrl);
            keyboard.Update();
        };
        const NS::Editor::UndoStack& undo = editor.Editor().Undo();

        // ギズモの道具のキーは、答えを Tick で受けるギズモを直に回して見る
        NS::Editor::GizmoEditor gizmo;
        gizmo.SetInput(&app.Input());
        gizmo.SetActive(true);
        ASSERT_NE(gizmo.Tool(), NS::Editor::GizmoTool::Rotate);

        for (const bool blocked : {true, false})
        {
            editor.SetEditShortcutsBlocked(blocked);

            const std::size_t beforeRotate = undo.UndoSize();
            press(NS::OS::Key::R, false);
            std::size_t expected = beforeRotate + 1;
            if (blocked)
            {
                expected = beforeRotate;
            }
            EXPECT_EQ(undo.UndoSize(), expected) << "R 止める=" << blocked;

            const std::size_t beforeUndo = undo.UndoSize();
            press(NS::OS::Key::Z, true);
            expected = beforeUndo - 1;
            if (blocked)
            {
                expected = beforeUndo;
            }
            EXPECT_EQ(undo.UndoSize(), expected) << "Ctrl+Z 止める=" << blocked;

            editor.Editor().Palette().SetActiveSlot(0);
            press(NS::OS::Key::Num2, false);
            std::size_t expectedSlot = 1;
            if (blocked)
            {
                expectedSlot = 0;
            }
            EXPECT_EQ(editor.Editor().Palette().ActiveSlot(), expectedSlot) << "番号 止める=" << blocked;

            const NS::Editor::GizmoTool toolBefore = gizmo.Tool();
            keyboard.OnKeyDown(NS::OS::Key::E);
            gizmo.Tick(NS::Matrix::Identity, view, true, blocked);
            keyboard.OnKeyUp(NS::OS::Key::E);
            keyboard.Update();
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
        keyboard.Update();
    }));
    EXPECT_EQ(app.Run(), 0);
}
