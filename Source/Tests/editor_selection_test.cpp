#include "Editor/EditorObjects.h"
#include "Editor/GizmoEditor.h"
#include "Editor/GridMath.h"
#include "Editor/InspectorPanel.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "Editor/PlacementCatalog.h"
#include "NSlib/App/Application.h"
#include "NSlib/App/Layer.h"
#include "NSlib/Object/SubObjects/TransformSubObject.h"
#include "NSlib/Object/ActorList.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Scene/SceneJson.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace
{
    class SelectionTestLayer : public NS::Layer
    {
    public:
        explicit SelectionTestLayer(std::function<void()> check) : m_check(std::move(check)) {}
        void OnAttach() override
        {
            m_check();
            NS::Application::Quit();
        }

    private:
        std::function<void()> m_check;
    };

    std::optional<NS::Vector2> FindMoveHandle(const NS::Obj::Actor& target,
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
    app.AddLayer(std::make_unique<SelectionTestLayer>([&app] {
        NS::Obj::Scene scene;
        LevelEditorController editor(&scene);
        const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("置物");
        ASSERT_NE(item, nullptr);
        editor.PlaceItem(*item);
        ASSERT_NE(editor.SelectedObjectId(), NS::Obj::k_NoObjectId);
        NS::OS::Keyboard& keyboard = app.Input().Keyboard();
        keyboard.ClearState();
        keyboard.Update();
        editor.Tick();

        keyboard.OnKeyDown(NS::OS::Key::Escape);
        editor.Tick();
        keyboard.OnKeyUp(NS::OS::Key::Escape);
        keyboard.Update();
        editor.Tick();
        EXPECT_EQ(editor.SelectedObjectId(), NS::Obj::k_NoObjectId);
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
    app.AddLayer(std::make_unique<SelectionTestLayer>([] {
        NS::Obj::Scene scene;
        LevelEditorController editor(&scene);
        const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("置物");
        ASSERT_NE(item, nullptr);
        editor.PlaceItem(*item);
        const std::uint32_t id = editor.SelectedObjectId();
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
    app.AddLayer(std::make_unique<SelectionTestLayer>([] {
        NS::Obj::Scene scene;
        LevelEditorController editor(&scene);
        const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("置物");
        ASSERT_NE(item, nullptr);
        editor.PlaceItem(*item);
        const std::uint32_t id = editor.SelectedObjectId();

        editor.EnterPlay();
        const NS::Quaternion rotation = NS::Quaternion::CreateFromYawPitchRoll(0.5f, 0.0f, 0.0f);
        editor.SetSelectedFreePosition(NS::Vector3{5.0f, 1.0f, 2.0f});
        editor.SetSelectedFreeRotation(rotation);
        editor.SetSelectedFreeScale(NS::Vector3{2.0f, 2.0f, 2.0f});

        const nlohmann::json& baseline = scene.PlayBaseline();
        const std::size_t index = NS::Obj::FindObjectIndexById(baseline, id);
        ASSERT_NE(index, NS::Obj::k_NoObjectIndex);
        const nlohmann::json& object = NS::Obj::SceneJsonObjects(baseline)[index];
        EXPECT_FLOAT_EQ(NS::Obj::ObjectPosition(object).x, 5.0f);
        EXPECT_FLOAT_EQ(NS::Obj::ObjectRotation(object).y, rotation.y);
        EXPECT_FLOAT_EQ(NS::Obj::ObjectScale(object).x, 2.0f);
        editor.EnterEdit();
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
    app.AddLayer(std::make_unique<SelectionTestLayer>([&app] {
        NS::Obj::Scene scene;
        LevelEditorController editor(&scene);
        editor.Setup(nullptr);
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

        const NS::Obj::Actor* camera = editor.SelectedObjectActor();
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

// 親子を一緒に選んで子を引いても、親は動かない
// 親も追従させると子が親ごと進み、毎フレーム差が膨らむ
TEST(EditorSelection, DraggingAChildLeavesTheSelectedParent)
{
    NS::ApplicationDesc desc;
    desc.window.title = "親子の引きの試し";
    desc.window.size = {320, 200};
    desc.window.visible = false;
    NS::Application app(desc);
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<SelectionTestLayer>([&app] {
        NS::Obj::Scene scene;
        LevelEditorController editor(&scene);
        editor.Setup(nullptr);
        const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("置物");
        ASSERT_NE(item, nullptr);
        editor.PlaceItem(*item);
        const std::uint32_t parentId = editor.SelectedObjectId();
        editor.PlaceItem(*item);
        const std::uint32_t childId = editor.SelectedObjectId();
        ASSERT_TRUE(editor.SetObjectParent(childId, parentId));
        editor.SelectObjects({parentId, childId}, childId);
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

        const auto worldPosition = [&scene](std::uint32_t id) {
            return scene.Objects().FindByObjectId(id)->Root().WorldMatrix().Translation();
        };
        const NS::Obj::Actor* child = scene.Objects().FindByObjectId(childId);
        ASSERT_NE(child, nullptr);
        const std::optional<NS::Vector2> handle =
            FindMoveHandle(*child, scene.GetCameraManager()->ViewProjection(), view);
        ASSERT_TRUE(handle.has_value());

        const NS::Vector3 parentBefore = worldPosition(parentId);
        const NS::Vector3 childBefore = worldPosition(childId);
        DragHandle(editor, mouse, *handle);
        EXPECT_NE(worldPosition(childId), childBefore);
        EXPECT_EQ(worldPosition(parentId), parentBefore);

        // 共有の入力を始めの位置と押していない状態へ戻す。残すと後の試しがマウスの動きを読む
        mouse.OnMove(startX, startY);
        mouse.ClearState();
        mouse.Update();
    }));
    EXPECT_EQ(app.Run(), 0);
}

// ずれた親の下の物も、世界の位置に出たギズモで引ける
TEST(EditorSelection, GizmoFollowsAChildUnderAnOffsetParent)
{
    NS::ApplicationDesc desc;
    desc.window.title = "親の下のギズモの試し";
    desc.window.size = {320, 200};
    desc.window.visible = false;
    NS::Application app(desc);
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<SelectionTestLayer>([&app] {
        NS::Obj::Scene scene;
        LevelEditorController editor(&scene);
        editor.Setup(nullptr);
        const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("置物");
        ASSERT_NE(item, nullptr);
        editor.PlaceItem(*item);
        const std::uint32_t parentId = editor.SelectedObjectId();
        const NS::Vector3 placed = scene.Objects().FindByObjectId(parentId)->Root().Position();
        editor.SetSelectedFreePosition(placed + NS::Vector3{3.0f, 0.0f, 0.0f});
        editor.PlaceItem(*item);
        const std::uint32_t childId = editor.SelectedObjectId();
        ASSERT_TRUE(editor.SetObjectParent(childId, parentId));
        editor.SelectObjectById(childId);
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

        const NS::Obj::Actor* child = scene.Objects().FindByObjectId(childId);
        ASSERT_NE(child, nullptr);
        ASSERT_NE(child->Root().Position(), child->Root().WorldMatrix().Translation());
        const std::optional<NS::Vector2> handle =
            FindMoveHandle(*child, scene.GetCameraManager()->ViewProjection(), view);
        ASSERT_TRUE(handle.has_value());

        const NS::Vector3 before = child->Root().WorldMatrix().Translation();
        DragHandle(editor, mouse, *handle);
        const NS::Vector3 after = scene.Objects().FindByObjectId(childId)->Root().WorldMatrix().Translation();
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
    app.AddLayer(std::make_unique<SelectionTestLayer>([] {
        NS::Obj::Scene scene;
        LevelEditorController editor(&scene);
        editor.Setup(nullptr);
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
    app.AddLayer(std::make_unique<SelectionTestLayer>([] {
        NS::Obj::Scene scene;
        LevelEditorController editor(&scene);
        editor.Setup(nullptr);
        editor.Editor().Palette().SetActiveSlot(0);
        editor.Editor().PlaceUnderCursorProgrammatic(0, 0, 0);
        ASSERT_EQ(scene.Objects().ObjectCount(), 1u);
        const std::uint32_t id = scene.Objects().ObjectAt(0)->Id();
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
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("置物");
    ASSERT_NE(item, nullptr);
    editor.PlaceItem(*item);
    const std::uint32_t first = editor.SelectedObjectId();
    editor.PlaceItem(*item);
    const std::uint32_t second = editor.SelectedObjectId();
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
