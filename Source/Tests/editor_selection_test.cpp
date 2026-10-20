#include "Editor/EditorObjects.h"
#include "Editor/GizmoEditor.h"
#include "Editor/GridMath.h"
#include "Editor/InspectorPanel.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "Editor/PlacementCatalog.h"
#include "Editor/Undo/ObjectSnapshotApplier.h"
#include "Game/Level/FollowCameraActor.h"
#include "NSlib/App/Application.h"
#include "NSlib/App/Layer.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/StringUtils.h"
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
#include <system_error>
#include <vector>

namespace
{
    std::optional<NS::Vector2> FindMoveHandle(const NS::SceneActor& target,
                                              const NS::Matrix& viewProjection,
                                              const NS::Editor::ViewRect& view)
    {
        const NS::AffineDecomposition world = NS::DecomposeAffine(target.Root().WorldMatrix());
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
        const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("置物");
        ASSERT_NE(item, nullptr);
        editor.PlaceItem(*item);
        ASSERT_NE(editor.SelectedObjectId(), NS::k_NoObjectId);
        NS::OS::Keyboard& keyboard = app.Input().Keyboard();
        keyboard.ClearState();
        keyboard.Update();
        editor.Tick();

        keyboard.OnKeyDown(NS::OS::Key::Escape);
        editor.Tick();
        keyboard.OnKeyUp(NS::OS::Key::Escape);
        keyboard.Update();
        editor.Tick();
        EXPECT_EQ(editor.SelectedObjectId(), NS::k_NoObjectId);
        EXPECT_TRUE(editor.SelectedObjectIds().empty());
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
        const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("置物");
        ASSERT_NE(item, nullptr);
        editor.PlaceItem(*item);
        const NS::uint32 id = editor.SelectedObjectId();
        const std::size_t undoBefore = editor.Editor().Undo().UndoSize();

        editor.EnterPlay();
        editor.DeleteSelectedObject();
        EXPECT_NE(scene.Objects().FindByObjectId(id), nullptr);
        editor.BeginTransformEdit();
        editor.SetSelectedFreePosition(NS::Vector3{5.0f, 0.0f, 0.0f});
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
        const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("置物");
        ASSERT_NE(item, nullptr);
        editor.PlaceItem(*item);
        const NS::uint32 id = editor.SelectedObjectId();

        editor.EnterPlay();
        const NS::Quaternion rotation = NS::Quaternion::CreateFromYawPitchRoll(0.5f, 0.0f, 0.0f);
        editor.SetSelectedFreePosition(NS::Vector3{5.0f, 1.0f, 2.0f});
        editor.SetSelectedFreeRotation(rotation);
        editor.SetSelectedFreeScale(NS::Vector3{2.0f, 2.0f, 2.0f});

        const nlohmann::json& baseline = scene.PlayBaseline();
        const std::size_t index = NS::FindObjectIndexById(baseline, id);
        ASSERT_NE(index, NS::k_NoObjectIndex);
        const nlohmann::json& object = NS::SceneJsonObjects(baseline)[index];
        EXPECT_FLOAT_EQ(NS::ObjectPosition(object).x, 5.0f);
        EXPECT_FLOAT_EQ(NS::ObjectRotation(object).y, rotation.y);
        EXPECT_FLOAT_EQ(NS::ObjectScale(object).x, 2.0f);
        editor.EnterEdit();
    }));
    EXPECT_EQ(app.Run(), 0);
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
        NS::Editor::ObjectSnapshotApplier applier(&scene);
        editor.Editor().SetApplier(&applier);
        const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("置物");
        ASSERT_NE(item, nullptr);
        editor.PlaceItem(*item);
        const NS::uint32 id = editor.SelectedObjectId();
        editor.Editor().Undo().Clear();
        ASSERT_FALSE(editor.Editor().HasUnsavedChanges());

        const auto colorOf = [&scene, id]() {
            const NS::SceneActor* actor = scene.Objects().FindByObjectId(id);
            return NS::SerializeClassObjFields(*actor->ModelClassObj())["基本色"];
        };
        const nlohmann::json colorBefore = colorOf();
        const NS::Vector3 positionBefore = scene.Objects().FindByObjectId(id)->Root().Position();

        // 何も変えずに戻る
        editor.EnterPlay();
        editor.EnterEdit();
        EXPECT_FALSE(editor.Editor().HasUnsavedChanges());

        // 位置だけがプレイで動く
        editor.EnterPlay();
        scene.Objects().FindByObjectId(id)->Root().SetPosition(NS::Vector3{7.0f, 3.0f, 1.0f});
        editor.EnterEdit();
        EXPECT_FALSE(editor.Editor().HasUnsavedChanges());
        EXPECT_EQ(scene.Objects().FindByObjectId(id)->Root().Position(), positionBefore);

        // インスペクタの道で色の欄を変える
        editor.EnterPlay();
        NS::SceneActor* actor = scene.Objects().FindByObjectId(id);
        ASSERT_NE(actor, nullptr);
        actor->ModelClassObj()->SetBaseColor(NS::Color{0.2f, 0.4f, 0.6f, 1.0f});
        const NS::ClassObjInfo* modelRow = nullptr;
        NS::ForEachReflectedClassObj(*actor, [&](const NS::ClassObjInfo& row, const NS::Object* part) {
            if (part == actor->ModelClassObj())
            {
                modelRow = &row;
            }
        });
        ASSERT_NE(modelRow, nullptr);
        editor.MirrorPlayEditToBaseline(*actor, *modelRow, "基本色");
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
        editor.Setup(nullptr, {});
        const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("追従カメラ");
        ASSERT_NE(item, nullptr);
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

        const NS::SceneActor* camera = editor.SelectedObjectActor();
        ASSERT_NE(camera, nullptr);
        const std::optional<NS::Vector2> handle =
            FindMoveHandle(*camera, scene.GetCameraManager()->ViewProjection(), view);
        ASSERT_TRUE(handle.has_value());

        const std::size_t undoBefore = editor.Editor().Undo().UndoSize();
        const NS::Vector3 before = camera->Root().Position();
        DragHandle(editor, mouse, *handle);
        EXPECT_NE(editor.SelectedObjectActor()->Root().Position(), before);
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
        editor.Setup(nullptr, {});
        const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("置物");
        ASSERT_NE(item, nullptr);
        editor.PlaceItem(*item);
        const NS::uint32 otherId = editor.SelectedObjectId();
        editor.PlaceItem(*item);
        const NS::uint32 actorId = editor.SelectedObjectId();
        editor.SelectObjects({otherId, actorId}, actorId);
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
            return scene.Objects().FindByObjectId(id)->Root().WorldMatrix().Translation();
        };
        const NS::SceneActor* actor = scene.Objects().FindByObjectId(actorId);
        ASSERT_NE(actor, nullptr);
        const std::optional<NS::Vector2> handle =
            FindMoveHandle(*actor, scene.GetCameraManager()->ViewProjection(), view);
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
        editor.Setup(nullptr, {});
        const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("置物");
        ASSERT_NE(item, nullptr);
        editor.PlaceItem(*item);
        const NS::uint32 actorId = editor.SelectedObjectId();
        const NS::Vector3 placed = scene.Objects().FindByObjectId(actorId)->Root().Position();
        editor.SetSelectedFreePosition(placed + NS::Vector3{3.0f, 0.0f, 0.0f});
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

        const NS::SceneActor* actor = scene.Objects().FindByObjectId(actorId);
        ASSERT_NE(actor, nullptr);
        ASSERT_EQ(actor->Root().Position(), actor->Root().WorldMatrix().Translation());
        const std::optional<NS::Vector2> handle =
            FindMoveHandle(*actor, scene.GetCameraManager()->ViewProjection(), view);
        ASSERT_TRUE(handle.has_value());

        const NS::Vector3 before = actor->Root().WorldMatrix().Translation();
        DragHandle(editor, mouse, *handle);
        const NS::Vector3 after = scene.Objects().FindByObjectId(actorId)->Root().WorldMatrix().Translation();
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
        editor.Setup(nullptr, {});
        editor.Editor().Palette().SetActiveSlot(2);
        ASSERT_STREQ(editor.Editor().Palette().CurrentTemplateName(), "ゴール");
        editor.Editor().PlaceUnderCursorProgrammatic(0, 0, 0);
        const std::size_t placed = scene.Objects().ObjectCount();
        editor.Editor().PlaceUnderCursorProgrammatic(0, 0, 0);
        EXPECT_EQ(scene.Objects().ObjectCount(), placed);
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
        editor.Setup(nullptr, {});
        editor.Editor().Palette().SetActiveSlot(0);
        editor.Editor().PlaceUnderCursorProgrammatic(0, 0, 0);
        ASSERT_EQ(scene.Objects().ObjectCount(), 1u);
        const NS::uint32 id = scene.Objects().ObjectAt(0)->Id();
        editor.SelectObjectById(id);
        editor.SetSelectedFreeRotation(NS::Quaternion::CreateFromYawPitchRoll(0.0f, 0.5f, 0.0f));
        const auto up = [&scene, id] {
            return NS::Vector3::Transform(NS::Vector3::UnitY, scene.Objects().FindByObjectId(id)->Root().Rotation());
        };
        const float tiltBefore = up().y;

        editor.Editor().RotateAtProgrammatic(0, 0, 0);
        EXPECT_NEAR(up().y, tiltBefore, 1.0e-4f);
    }));
    EXPECT_EQ(app.Run(), 0);
}

// 名前を打つ途中で選択を変えても、打った名前は打ち始めた物へ入る。後から選んだ物は改名しない
TEST(EditorSelection, RenameGoesToTheObjectBeingTyped)
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
    const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("置物");
    ASSERT_NE(item, nullptr);
    editor.PlaceItem(*item);
    const NS::uint32 first = editor.SelectedObjectId();
    editor.PlaceItem(*item);
    const NS::uint32 second = editor.SelectedObjectId();
    ASSERT_NE(first, second);
    const std::string secondName = NS::Editor::ObjectDisplayName(*scene.Objects().FindByObjectId(second));
    editor.SelectObjectById(first);

    NS::Editor::InspectorPanel inspector;
    const auto frame = [&] {
        ImGui::NewFrame();
        inspector.Render(editor);
        ImGui::EndFrame();
    };
    frame();
    ImGuiWindow* window = ImGui::FindWindowByName(NS::Editor::k_PanelInspector);
    ASSERT_NE(window, nullptr);
    ImGui::ActivateItemByID(window->GetID("##objectName"));
    frame();
    frame();
    io.AddInputCharactersUTF8("Typed");
    frame();

    editor.SelectObjectById(second);
    ImGui::NewFrame();
    ImGui::ClearActiveID();
    inspector.Render(editor);
    ImGui::EndFrame();

    EXPECT_NE(std::string{NS::Editor::ObjectDisplayName(*scene.Objects().FindByObjectId(first))}.find("Typed"),
              std::string::npos);
    EXPECT_EQ(NS::Editor::ObjectDisplayName(*scene.Objects().FindByObjectId(second)), secondName);
    ImGui::DestroyContext(gui);
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
    NS::Editor::ObjectSnapshotApplier applier(&scene);
    editor.Editor().SetApplier(&applier);
    editor.PlaceItem(*NS::Editor::FindPlacementItem("置物"));
    const NS::uint32 id = editor.SelectedObjectId();
    const NS::Vector3 beforePosition = editor.SelectedObjectActor()->Root().Position();
    const NS::Vector3 beforeScale = editor.SelectedObjectActor()->Root().Scale();
    const NS::Quaternion beforeRotation = editor.SelectedObjectActor()->Root().Rotation();
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
    const char* rows[] = {"position", "rotation", "scale"};
    const char* values[] = {"12.5", "30", "2.5"};
    for (int row = 0; row < 3; ++row)
    {
        ImGuiWindow* window = ImGui::FindWindowByName(NS::Editor::k_PanelInspector);
        ImGuiID inputId = ImHashStr("##transform", 0, window->ID);
        inputId = ImHashStr(rows[row], 0, inputId);
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
    const NS::Transform& edited = scene.Objects().FindByObjectId(id)->Root();
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
    const NS::Transform& restored = scene.Objects().FindByObjectId(id)->Root();
    EXPECT_EQ(restored.Position(), beforePosition);
    EXPECT_EQ(restored.Rotation(), beforeRotation);
    EXPECT_EQ(restored.Scale(), beforeScale);
    ImGui::DestroyContext(gui);
}

TEST(EditorSelection, AssetAndActorSelectionsExcludeEachOther)
{
    NS::Scene scene;
    LevelEditorController editor(&scene);
    const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("置物");
    ASSERT_NE(item, nullptr);
    editor.PlaceItem(*item);
    const NS::uint32 placed = editor.SelectedObjectId();
    ASSERT_NE(placed, NS::k_NoObjectId);

    editor.SelectAsset(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets/Materials/stone.mat"));
    EXPECT_EQ(editor.SelectedObjectId(), NS::k_NoObjectId);
    EXPECT_TRUE(editor.SelectedObjectIds().empty());
    EXPECT_EQ(editor.SelectedAssetPath(), "Assets/Materials/stone.mat");
    EXPECT_NE(NS::Cast<NS::Material>(editor.SelectedAsset()), nullptr);

    editor.SelectObjectById(placed);
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
        editor.Setup(nullptr, {});
        const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("置物");
        ASSERT_NE(item, nullptr);
        editor.PlaceItem(*item);
        const NS::uint32 actorId = editor.SelectedObjectId();
        ASSERT_NE(actorId, NS::k_NoObjectId);
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
            {"アクターの選択", [&editor, actorId] { editor.SelectObjectById(actorId); }},
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
                 EXPECT_NE(editor.SelectedObjectId(), NS::k_NoObjectId);
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
        editor.Setup(nullptr, {});
        const NS::Editor::PlacementItem* prop = NS::Editor::FindPlacementItem("置物");
        const NS::Editor::PlacementItem* camera = NS::Editor::FindPlacementItem("追従カメラ");
        ASSERT_NE(prop, nullptr);
        ASSERT_NE(camera, nullptr);
        const NS::Editor::UndoStack& undo = editor.Editor().Undo();

        // 置く: 作った物を選ぶ
        std::size_t objectsBefore = scene.Objects().ObjectCount();
        std::size_t undoBefore = undo.UndoSize();
        editor.PlaceItem(*prop);
        const NS::uint32 placed = editor.SelectedObjectId();
        EXPECT_NE(scene.Objects().FindByObjectId(placed), nullptr);
        EXPECT_EQ(scene.Objects().ObjectCount(), objectsBefore + 1);
        EXPECT_EQ(undo.UndoSize(), undoBefore + 1);
        EXPECT_TRUE(editor.Editor().HasUnsavedChanges());
        EXPECT_TRUE(editor.Editor().PerformUndo());
        EXPECT_EQ(scene.Objects().FindByObjectId(placed), nullptr);
        EXPECT_EQ(scene.Objects().ObjectCount(), objectsBefore);

        // 筆で空いた升に置く: 今の選択を変えない
        editor.PlaceItem(*prop);
        const NS::uint32 kept = editor.SelectedObjectId();
        editor.Editor().Palette().SetActiveSlot(0);
        objectsBefore = scene.Objects().ObjectCount();
        undoBefore = undo.UndoSize();
        editor.Editor().PlaceUnderCursorProgrammatic(10, 0, 10);
        EXPECT_NE(NS::Editor::FindObjectIdAtCell(scene.Objects(), 10, 0, 10), NS::k_NoObjectId);
        EXPECT_EQ(scene.Objects().ObjectCount(), objectsBefore + 1);
        EXPECT_EQ(undo.UndoSize(), undoBefore + 1);
        EXPECT_TRUE(editor.Editor().HasUnsavedChanges());
        EXPECT_EQ(editor.SelectedObjectId(), kept);
        EXPECT_TRUE(editor.Editor().PerformUndo());
        EXPECT_EQ(NS::Editor::FindObjectIdAtCell(scene.Objects(), 10, 0, 10), NS::k_NoObjectId);
        EXPECT_EQ(scene.Objects().ObjectCount(), objectsBefore);

        // 複製: 写した物どうしの参照は写した先を指し、写した物を選ぶ
        editor.PlaceItem(*camera);
        const NS::uint32 follower = editor.SelectedObjectId();
        GL::Level::FollowCameraActor* followActor = AsFollowCamera(scene.Objects().FindByObjectId(follower));
        ASSERT_NE(followActor, nullptr);
        ASSERT_EQ(NS::ApplyJsonFields(followActor->Vcam(), nlohmann::json{{"追従対象", {{"ref", kept}}}}), 0u);
        editor.SelectObjects({kept, follower}, follower);
        objectsBefore = scene.Objects().ObjectCount();
        undoBefore = undo.UndoSize();
        editor.DuplicateSelectedObject();
        EXPECT_EQ(scene.Objects().ObjectCount(), objectsBefore + 2);
        EXPECT_EQ(undo.UndoSize(), undoBefore + 1);
        EXPECT_TRUE(editor.Editor().HasUnsavedChanges());
        const std::vector<NS::uint32> copies = editor.SelectedObjectIds();
        ASSERT_EQ(copies.size(), 2u);
        EXPECT_NE(copies[0], kept);
        EXPECT_NE(copies[1], follower);
        const GL::Level::FollowCameraActor* followCopy = AsFollowCamera(scene.Objects().FindByObjectId(copies[1]));
        ASSERT_NE(followCopy, nullptr);
        EXPECT_EQ(followCopy->Vcam().TargetRef().id, copies[0]);
        EXPECT_TRUE(editor.Editor().PerformUndo());
        EXPECT_EQ(scene.Objects().FindByObjectId(copies[0]), nullptr);
        EXPECT_EQ(scene.Objects().FindByObjectId(copies[1]), nullptr);
        EXPECT_EQ(scene.Objects().ObjectCount(), objectsBefore);

        // プレイ中: どの道も作らず、履歴も積まない
        editor.SelectObjects({kept}, kept);
        editor.EnterPlay();
        objectsBefore = scene.Objects().ObjectCount();
        undoBefore = undo.UndoSize();
        editor.PlaceItem(*prop);
        editor.DuplicateSelectedObject();
        editor.Editor().PlaceUnderCursorProgrammatic(12, 0, 12);
        EXPECT_EQ(scene.Objects().ObjectCount(), objectsBefore);
        EXPECT_EQ(undo.UndoSize(), undoBefore);
        editor.EnterEdit();
    }));
    EXPECT_EQ(app.Run(), 0);
}
