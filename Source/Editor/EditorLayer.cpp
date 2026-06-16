#include "Editor/EditorLayer.h"

#include "Framework/App/Application.h"
#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Graphics/RenderSettings.h"
#include "Framework/Platform/Gamepad.h"
#include "Framework/Platform/Input.h"
#include "Framework/Platform/Keyboard.h"
#include "Framework/Platform/Window.h"
#include "Framework/UI/ImGuiContext.h"
#include "Game/Blocks/BlockRegistry.h"
#include "Game/Game.h"
#include "Editor/LevelEditorController.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

EditorLayer::EditorLayer() : NS::App::Layer("EditorLayer") {}
EditorLayer::~EditorLayer() = default;

void EditorLayer::OnAttach()
{
    // 起動 scene は Game レイヤが既に load + OnStart 済 (SceneManager::LoadScene が同期実行)
    auto* app = NS::App::Application::Get();
    auto* game = Game::Get();
    auto* scene = game ? game->CurrentPlayScene() : nullptr;
    if (app == nullptr || scene == nullptr)
    {
        NS_LOG_ERROR(::NS::Core::LogCat::App, "EditorLayer::OnAttach: app / play scene 不在のため編集を起動できない");
        return;
    }

    // ImGui ライフサイクルを Layer が所有する。Application は UI を知らないので editor が立ち上げる
    m_imgui = std::make_unique<NS::UI::ImGuiContext>(app->Window(), app->Renderer());
    if (!m_imgui->IsValid())
        NS_LOG_ERROR(::NS::Core::LogCat::App, "ImGuiContext 構築失敗、 編集 UI は機能しない");

    // 生 Win32 メッセージを ImGui へ転送するフックを Window に登録する (Platform は中身を知らない)
    app->Window().SetMessageHook(
        [imgui = m_imgui.get()](void* hwnd, std::uint32_t msg, std::uintptr_t wParam, std::intptr_t lParam) {
            if (imgui != nullptr)
                (void)imgui->ForwardWndProc(hwnd, msg, wParam, lParam);
        });

    m_controller = std::make_unique<LevelEditorController>(scene);
    m_controller->Setup(m_imgui.get());
    NS_LOG_INFO(::NS::Core::LogCat::App, "EditorLayer attached (Debug/Dev/GameDebug only)");
}

void EditorLayer::OnDetach()
{
    // scene 破棄 (Game::OnDetach) より先に呼ばれる順序 (overlay は逆順で OnDetach) なので安全に片付く
    if (m_controller)
        m_controller->Teardown();
    m_controller.reset();

    // ImGui を畳む前に hook を外し、 WndProc から dangling な context を踏まないようにする
    if (auto* app = NS::App::Application::Get())
    {
        app->Window().SetMessageHook(nullptr);
        app->Input().SetUiCapture(false, false);
    }
    // ImGui_ImplDX11_Shutdown が ID3D11Device を要求するため Renderer 健在の今 (Application::Shutdown より前) に破棄
    m_imgui.reset();
    NS_LOG_INFO(::NS::Core::LogCat::App, "EditorLayer detached");
}

void EditorLayer::OnUpdate()
{
    if (!IsActive() || !m_controller)
        return;

    // 編集ロジック (free-fly カメラ / ギズモ / EditorMode / クリア監視) を先に回す
    m_controller->Tick();
    HandleModeToggleInput(*m_controller);
    HandlePauseInput(*m_controller);
}

void EditorLayer::OnRender()
{
    if (!IsActive() || !m_controller || !m_imgui)
        return;
    LevelEditorController& editor = *m_controller;

    // ImGui の 1 フレームを Layer が囲う。Renderer::BeginFrame 済の RT へ EndFrame (Render) が描く
    m_imgui->BeginFrame();

    // 編集用の上乗せ描画 (ギズモ / palette / 編集ビジュアル) と debug provenance 退避
    editor.Render();

    if (editor.CurrentMode() == LevelEditorController::Mode::Edit)
    {
        RenderDockSpaceHost();
        editor.Editor().RenderFileBrowser();
        RenderToolModePanel(editor);
        RenderHierarchyPanel(editor);
        RenderInspectorPanel(editor);
        RenderMaterialsPanel(editor);
    }
    else if (editor.Play().paused)
        RenderPauseModal(editor);

    RenderFpsOverlay();
    RenderRenderSettingsPanel(editor);

    m_imgui->EndFrame();

    // 次フレームの gameplay / Window 入力ゲート用に UI キャプチャ状態を Input へ反映する
    if (auto* app = NS::App::Application::Get())
        app->Input().SetUiCapture(m_imgui->WantCaptureMouse(), m_imgui->WantCaptureKeyboard());
}

void EditorLayer::HandleModeToggleInput(LevelEditorController& editor) noexcept
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;
    auto& input = app->Input();

    // UI がキーボードを握っている間は mode flip させない
    const bool wantKb = input.UiWantsKeyboard();

    const bool tabPressed = !wantKb && input.Keyboard().IsPressed(NS::Platform::Key::Tab);
    const bool startPressed =
        input.Gamepad(0).IsConnected() && input.Gamepad(0).IsPressed(NS::Platform::GamepadButton::Start);

    if (tabPressed || startPressed)
    {
        if (editor.CurrentMode() == LevelEditorController::Mode::Edit)
            editor.EnterPlay();
        else
            editor.EnterEdit();
    }
}

void EditorLayer::HandlePauseInput(LevelEditorController& editor) noexcept
{
    if (editor.CurrentMode() != LevelEditorController::Mode::Play)
        return;
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;
    auto& input = app->Input();

    const bool wantKb = input.UiWantsKeyboard();

    const bool pPressed = !wantKb && input.Keyboard().IsPressed(NS::Platform::Key::P);
    const bool backPressed =
        input.Gamepad(0).IsConnected() && input.Gamepad(0).IsPressed(NS::Platform::GamepadButton::Back);

    if (pPressed || backPressed)
        editor.Play().paused = !editor.Play().paused;
}

void EditorLayer::RenderDockSpaceHost() noexcept
{
#if NS_EDITOR_ENABLED
    // 中央ノードは透過 (背景非描画 + 入力素通し) なので、 奥の全画面 3D とギズモがそのまま見え
    // 中央クリックは編集に届く。 周囲に各パネルがドッキングできる。 dockspace_id=0 で viewport から自動生成
    ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
#endif
}

void EditorLayer::RenderFpsOverlay() noexcept
{
#if NS_EDITOR_ENABLED
    const auto vp = ImGui::GetMainViewport();
    if (vp == nullptr)
        return;
    // pivot=(1,0) で右端固定。 width が不確定でも右端から padding 分だけ内側に収まる
    constexpr float kPadding = 10.0f;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - kPadding, vp->WorkPos.y + kPadding),
                            ImGuiCond_Always,
                            ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.35f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs |
                                        ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##FpsOverlay", nullptr, kFlags))
    {
        const ImGuiIO& io = ImGui::GetIO();
        const float fps = io.Framerate;
        const float ms = (fps > 0.0f) ? (1000.0f / fps) : 0.0f;
        ImGui::Text("%.1f FPS (%.2f ms)", static_cast<double>(fps), static_cast<double>(ms));
    }
    ImGui::End();
#endif
}

void EditorLayer::RenderRenderSettingsPanel(LevelEditorController& editor) noexcept
{
#if NS_EDITOR_ENABLED
    const NS::Graphics::RenderSettings& resolved = editor.DebugResolvedSettings();
    const NS::Graphics::RenderSettingsOverride& sceneOver = editor.DebugSceneOverride();
    const NS::Graphics::RenderSettingsOverride& objOver = editor.DebugPlayerObjectOverride();

    // 出所は has_value の突き合わせで逆算する。 Resolve のホットパスに追跡を入れない
    auto provenance = [](bool sceneHas, bool objectHas) -> const char* {
        if (objectHas)
            return "object";
        if (sceneHas)
            return "scene";
        return "default";
    };

    if (ImGui::Begin("RenderSettings"))
    {
        ImGui::Text("lightDir   : (%.2f,%.2f,%.2f) [%s]",
                    static_cast<double>(resolved.lightDir.x),
                    static_cast<double>(resolved.lightDir.y),
                    static_cast<double>(resolved.lightDir.z),
                    provenance(sceneOver.lightDir.has_value(), objOver.lightDir.has_value()));
        ImGui::Text("lightColor : (%.2f,%.2f,%.2f) [%s]",
                    static_cast<double>(resolved.lightColor.x),
                    static_cast<double>(resolved.lightColor.y),
                    static_cast<double>(resolved.lightColor.z),
                    provenance(sceneOver.lightColor.has_value(), objOver.lightColor.has_value()));
        ImGui::Text("ambient    : (%.2f,%.2f,%.2f) [%s]",
                    static_cast<double>(resolved.ambientColor.x),
                    static_cast<double>(resolved.ambientColor.y),
                    static_cast<double>(resolved.ambientColor.z),
                    provenance(sceneOver.ambientColor.has_value(), objOver.ambientColor.has_value()));
        ImGui::Separator();
        ImGui::Text("clearColor : (%.2f,%.2f,%.2f,%.2f) [%s]",
                    static_cast<double>(resolved.clearColor.R()),
                    static_cast<double>(resolved.clearColor.G()),
                    static_cast<double>(resolved.clearColor.B()),
                    static_cast<double>(resolved.clearColor.A()),
                    provenance(sceneOver.clearColor.has_value(), objOver.clearColor.has_value()));
        ImGui::TextDisabled("clearColor / vsync のシーン上書きは非対応 (lighting 3 種のみ階層対応)");
    }
    ImGui::End();
#else
    (void)editor;
#endif
}

void EditorLayer::RenderToolModePanel(LevelEditorController& editor) noexcept
{
#if NS_EDITOR_ENABLED
    // 位置はドッキング / imgui.ini 任せ (固定座標を置くとドッキング配置と競合する)
    if (ImGui::Begin("Edit Mode"))
    {
        const bool objectActive = editor.ObjectToolActive();
        if (ImGui::RadioButton("Build (Grid place)", !objectActive))
            editor.SetObjectToolActive(false);
        if (ImGui::RadioButton("Object (Gizmo)", objectActive))
            editor.SetObjectToolActive(true);
        ImGui::Separator();
        if (objectActive)
            ImGui::TextUnformatted("Click orange box to select. Q/W/E/R = Select/Move/Rotate/Scale");
        else
            ImGui::TextUnformatted("Left click = place block");
    }
    ImGui::End();
#else
    (void)editor;
#endif
}

void EditorLayer::RenderHierarchyPanel(LevelEditorController& editor) noexcept
{
#if NS_EDITOR_ENABLED
    if (ImGui::Begin("Hierarchy"))
    {
        const auto& objects = editor.Level().objects;
        const std::size_t selected = editor.SelectedObjectIndex();

        ImGui::Text("%zu objects", objects.size());
        ImGui::Separator();

        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            const NS::Game::Level::ObjectInstance& object = objects[i];
            const bool grid = (object.flags & NS::Game::Level::kObjectFlagGridAligned) != 0;
            const char* name = NS::Game::Blocks::GetDisplayName(object.kind);

            char label[96];
            std::snprintf(label, sizeof(label), "[%zu] %s (%s)", i, name, grid ? "grid" : "free");

            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(label, i == selected))
                editor.SelectObjectByIndex(i);
            ImGui::PopID();
        }

        if (objects.empty())
            ImGui::TextDisabled("(no objects)");

        ImGui::Separator();
        const auto& cameras = editor.Level().cameraVolumes;
        const std::size_t selectedCamera = editor.SelectedCameraIndex();
        ImGui::Text("%zu area cameras", cameras.size());
        if (ImGui::SmallButton("+ Add Camera"))
            editor.AddCameraVolume();

        for (std::size_t i = 0; i < cameras.size(); ++i)
        {
            char label[64];
            std::snprintf(label, sizeof(label), "[cam %zu] priority %d", i, cameras[i].priority);

            ImGui::PushID(static_cast<int>(i) + 100000); // object 添字と ID 衝突しないようずらす
            if (ImGui::Selectable(label, i == selectedCamera))
                editor.SelectCameraByIndex(i);
            ImGui::PopID();
        }

        if (cameras.empty())
            ImGui::TextDisabled("(no area cameras)");
    }
    ImGui::End();
#else
    (void)editor;
#endif
}

void EditorLayer::RenderInspectorPanel(LevelEditorController& editor) noexcept
{
#if NS_EDITOR_ENABLED
    if (ImGui::Begin("Inspector"))
    {
        if (editor.HasCameraSelection())
        {
            NS::Game::Level::CameraVolume cam = editor.SelectedCameraSnapshot();
            ImGui::Text("[cam %zu] area camera", editor.SelectedCameraIndex());
            ImGui::Separator();

            bool changed = false;
            float camPos[3] = {cam.cameraPositionX, cam.cameraPositionY, cam.cameraPositionZ};
            if (ImGui::DragFloat3("Camera Pos", camPos, 0.05f))
            {
                cam.cameraPositionX = camPos[0];
                cam.cameraPositionY = camPos[1];
                cam.cameraPositionZ = camPos[2];
                changed = true;
            }
            float look[3] = {cam.lookTargetX, cam.lookTargetY, cam.lookTargetZ};
            if (ImGui::DragFloat3("Look Target", look, 0.05f))
            {
                cam.lookTargetX = look[0];
                cam.lookTargetY = look[1];
                cam.lookTargetZ = look[2];
                changed = true;
            }
            float center[3] = {cam.triggerCenterX, cam.triggerCenterY, cam.triggerCenterZ};
            if (ImGui::DragFloat3("Trigger Center", center, 0.05f))
            {
                cam.triggerCenterX = center[0];
                cam.triggerCenterY = center[1];
                cam.triggerCenterZ = center[2];
                changed = true;
            }
            float extent[3] = {cam.triggerExtentX, cam.triggerExtentY, cam.triggerExtentZ};
            if (ImGui::DragFloat3("Trigger Extent", extent, 0.05f))
            {
                // 半径が 0 以下だと判定が常に外れるので最小正値に clamp する
                cam.triggerExtentX = extent[0] > 0.01f ? extent[0] : 0.01f;
                cam.triggerExtentY = extent[1] > 0.01f ? extent[1] : 0.01f;
                cam.triggerExtentZ = extent[2] > 0.01f ? extent[2] : 0.01f;
                changed = true;
            }
            int priority = cam.priority;
            if (ImGui::DragInt("Priority", &priority, 0.2f, -1000, 1000))
            {
                cam.priority = priority;
                changed = true;
            }
            bool lookAtPlayer = cam.lookAtPlayer != 0;
            if (ImGui::Checkbox("Look At Player", &lookAtPlayer))
            {
                cam.lookAtPlayer = lookAtPlayer; // bool → uint8_t は 0/1 で安全
                changed = true;
            }

            if (changed)
                editor.SetSelectedCameraVolume(cam);

            ImGui::Separator();
            if (ImGui::Button("Delete Camera"))
                editor.DeleteSelectedCamera();

            ImGui::End();
            return;
        }

        if (!editor.HasInspectableSelection())
        {
            ImGui::TextDisabled("(no selection)");
            ImGui::End();
            return;
        }

        const NS::Game::Level::ObjectInstance obj = editor.SelectedObjectSnapshot();
        const bool grid = editor.SelectedIsGridAligned();
        ImGui::Text("[%zu] %s (%s)",
                    editor.SelectedObjectIndex(),
                    NS::Game::Blocks::GetDisplayName(obj.kind),
                    grid ? "grid" : "free");
        ImGui::Separator();

        if (grid)
        {
            ImGui::Text("Cell: (%d, %d, %d)",
                        static_cast<int>(NS::Game::Level::ObjectCellX(obj)),
                        static_cast<int>(NS::Game::Level::ObjectCellY(obj)),
                        static_cast<int>(NS::Game::Level::ObjectCellZ(obj)));
            if (obj.kind == NS::Game::Blocks::kBlockIdSolid)
            {
                ImGui::TextDisabled("Promote to free to edit transform");
                if (ImGui::Button("Promote to Free"))
                    editor.PromoteSelectedToFree();
            }
            else
                ImGui::TextDisabled("grid object (no gizmo/edit in v1)");
        }
        else
        {
            // free オブジェクトは runtime Transform が真実の源なので毎フレーム即反映する (SyncFreeObjectTransforms
            // が永続化)
            float pos[3] = {obj.positionX, obj.positionY, obj.positionZ};
            if (ImGui::DragFloat3("Position", pos, 0.05f))
                editor.SetSelectedFreePosition(NS::Math::Vector3{pos[0], pos[1], pos[2]});

            float scl[3] = {obj.scaleX, obj.scaleY, obj.scaleZ};
            if (ImGui::DragFloat3("Scale", scl, 0.05f))
                editor.SetSelectedFreeScale(NS::Math::Vector3{scl[0], scl[1], scl[2]});

            ImGui::Text("Rotation: (%.2f, %.2f, %.2f, %.2f)",
                        static_cast<double>(obj.rotationX),
                        static_cast<double>(obj.rotationY),
                        static_cast<double>(obj.rotationZ),
                        static_cast<double>(obj.rotationW));
            ImGui::TextDisabled("rotate with gizmo R tool");
        }

        ImGui::Separator();
        // 材質の適用は Assets パネルのドロップ / クリック。 ここでは現在値の表示のみ
        if (obj.materialIndex >= 0 && static_cast<std::size_t>(obj.materialIndex) < editor.Level().materialPaths.size())
            ImGui::Text("Material: %s",
                        editor.Level().materialPaths[static_cast<std::size_t>(obj.materialIndex)].c_str());
        else
            ImGui::TextDisabled("Material: default");
    }
    ImGui::End();
#else
    (void)editor;
#endif
}

void EditorLayer::RenderMaterialsPanel(LevelEditorController& editor) noexcept
{
#if NS_EDITOR_ENABLED
    // Object モード専用 (適用先のギズモ選択は Object モードにしか存在しない)
    if (!editor.ObjectToolActive())
        return;

    if (ImGui::Begin("Assets"))
    {
        const bool hasSelection = editor.HasGizmoSelection();
        ImGui::TextUnformatted(hasSelection ? "Selected object: yes" : "Select an object first (click it)");

        // ドロップ枠: ツリーの .mat をここへドラッグすると選択中の物体へ適用する
        ImGui::Button(hasSelection ? "Drop .mat here -> apply to selected" : "Drop target (needs selection)",
                      ImVec2(-1.0f, 32.0f));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("NS_MATERIAL"))
            {
                const char* droppedPath = static_cast<const char*>(payload->Data);
                editor.ApplyMaterialToSelected(std::filesystem::path(droppedPath));
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::Separator();
        // Assets/ 以下をフォルダツリーで表示する。 .mat はクリック適用 / ドラッグ可
        RenderAssetTree(NS::Core::FileSystem::GetExeDirectory() / "Assets", editor);
    }
    ImGui::End();
#else
    (void)editor;
#endif
}

void EditorLayer::RenderAssetTree(const std::filesystem::path& dir, LevelEditorController& editor) noexcept
{
#if NS_EDITOR_ENABLED
    const bool hasSelection = editor.HasGizmoSelection();

    // サブフォルダを TreeNode で再帰表示する (open 時のみ中身を走査する遅延読み)
    for (const auto& sub : NS::Core::FileSystem::ListDirectories(dir))
    {
        const std::string label = sub.filename().string();
        if (ImGui::TreeNode(label.c_str()))
        {
            RenderAssetTree(sub, editor);
            ImGui::TreePop();
        }
    }

    // フォルダ直下のファイル。 .mat はクリックで選択物体へ適用 + ドラッグ可、 他は読み取り専用表示
    for (const auto& file : NS::Core::FileSystem::ListFiles(dir))
    {
        const std::string name = file.filename().string();
        if (file.extension() != ".mat")
        {
            ImGui::TextDisabled("%s", name.c_str());
            continue;
        }
        ImGui::PushID(name.c_str());
        if (ImGui::Selectable(name.c_str()) && hasSelection)
            editor.ApplyMaterialToSelected(file);
        if (ImGui::BeginDragDropSource())
        {
            const std::string full = file.string();
            ImGui::SetDragDropPayload("NS_MATERIAL", full.c_str(), full.size() + 1);
            ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
        }
        ImGui::PopID();
    }
#else
    (void)dir;
    (void)editor;
#endif
}

void EditorLayer::RenderPauseModal(LevelEditorController& editor) noexcept
{
#if NS_EDITOR_ENABLED
    // paused フラグ単独で状態を表現するため、 modal の閉じ X は不要
    const auto vp = ImGui::GetMainViewport();
    if (vp != nullptr)
    {
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                                ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
    }
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("Paused", nullptr, kFlags))
    {
        ImGui::TextUnformatted("Paused");
        ImGui::Separator();
        if (ImGui::Button("Resume", ImVec2(160.0f, 0.0f)))
            editor.Play().paused = false;
        if (ImGui::Button("Quit to Edit", ImVec2(160.0f, 0.0f)))
            editor.EnterEdit();
    }
    ImGui::End();
#else
    (void)editor;
#endif
}
