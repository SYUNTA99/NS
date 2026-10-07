#include "Editor/EditorObjects.h"
#include "Editor/GizmoEditor.h"
#include "Editor/GridMath.h"
#include "Editor/InspectorPanel.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "Editor/PlacementCatalog.h"
#include "NSlib/App/Application.h"
#include "NSlib/App/Layer.h"
#include "NSlib/Object/ObjectList.h"
#include "NSlib/Object/Scene/Scene.h"

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
        const NS::Matrix viewProjection = scene.GetCameraManager()->ViewProjection();
        std::optional<NS::Vector2> handle;
        for (int y = 0; y < view.height && !handle; ++y)
        {
            for (int x = 0; x < view.width; ++x)
            {
                const NS::Vector2 point{static_cast<float>(x), static_cast<float>(y)};
                if (NS::Editor::GizmoEditor::ToolHandlePick(camera->Root().Position(),
                                                            camera->Root().Rotation(),
                                                            NS::Editor::GizmoTool::Move,
                                                            point,
                                                            viewProjection,
                                                            NS::Editor::ViewRectSize(view)) !=
                    NS::Editor::GizmoAxis::None)
                {
                    handle = point;
                    break;
                }
            }
        }
        ASSERT_TRUE(handle.has_value());

        const std::size_t undoBefore = editor.Editor().Undo().UndoSize();
        const NS::Vector3 before = camera->Root().Position();
        mouse.OnMove(static_cast<int>(handle->x), static_cast<int>(handle->y));
        mouse.OnButtonDown(NS::OS::MouseButton::Left);
        editor.Tick();
        mouse.Update();
        mouse.OnMove(static_cast<int>(handle->x) + 30, static_cast<int>(handle->y) + 20);
        editor.Tick();
        mouse.Update();
        mouse.OnButtonUp(NS::OS::MouseButton::Left);
        editor.Tick();
        mouse.Update();
        EXPECT_NE(editor.SelectedObjectActor()->Root().Position(), before);
        EXPECT_EQ(editor.Editor().Undo().UndoSize(), undoBefore + 1);

        // 共有の入力を始めの位置と押していない状態へ戻す。残すと後の試しがマウスの動きを読む
        mouse.OnMove(startX, startY);
        mouse.ClearState();
        mouse.Update();
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
