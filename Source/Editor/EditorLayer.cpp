#include "Editor/EditorLayer.h"

#include "Editor/InspectorReflection.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PlayerTuningIO.h"
#include "Framework/App/Application.h"
#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Graphics/RenderSettings.h"
#include "Framework/Platform/Gamepad.h"
#include "Framework/Platform/Input.h"
#include "Framework/Platform/Keyboard.h"
#include "Framework/Platform/Window.h"
#include "Framework/Scene/ComponentRegistry.h"
#include "Framework/Scene/Components/PlacedVirtualCamera.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/UI/ImGuiContext.h"
#include "Game/Blocks/BuildPlacedObject.h"
#include "Game/Game.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

EditorLayer::EditorLayer() : NS::App::Layer("EditorLayer") {}
EditorLayer::~EditorLayer() = default;

void EditorLayer::OnAttach()
{
    // SceneManager::LoadScene が同期実行するので起動 scene は Game レイヤが既に load + OnStart 済
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

    // 生 Win32 メッセージを ImGui へ転送するフックを Window に登録する。 Platform は中身を知らない
    app->Window().SetMessageHook(
        [imgui = m_imgui.get()](void* hwnd, std::uint32_t msg, std::uintptr_t wParam, std::intptr_t lParam) {
            if (imgui != nullptr)
                (void)imgui->ForwardWndProc(hwnd, msg, wParam, lParam);
        });

    m_controller = std::make_unique<LevelEditorController>(scene);
    m_controller->Setup(m_imgui.get());

    // 終了要求を握って保存確認を挟む。 出荷には EditorLayer が無いのでリリースは確認なしで終了する
    app->SetQuitGuard([this]() { return OnQuitRequested(); });

    NS_LOG_INFO(::NS::Core::LogCat::App, "EditorLayer attached (Debug/Dev/GameDebug only)");
}

void EditorLayer::OnDetach()
{
    // scene 破棄の Game::OnDetach より先に呼ばれる順序で、 overlay は逆順で OnDetach されるので安全に片付く
    if (m_controller)
        m_controller->Teardown();
    m_controller.reset();

    // ImGui を畳む前に hook を外し、 WndProc から無効になった context を踏まないようにする
    if (auto* app = NS::App::Application::Get())
    {
        app->SetQuitGuard(nullptr);
        app->Window().SetMessageHook(nullptr);
        app->Input().SetUiCapture(false, false);
    }
    // ImGui_ImplDX11_Shutdown が ID3D11Device を要求するため Renderer 健在で Application::Shutdown より前の今に破棄
    m_imgui.reset();
    NS_LOG_INFO(::NS::Core::LogCat::App, "EditorLayer detached");
}

void EditorLayer::OnUpdate()
{
    if (!IsActive() || !m_controller)
        return;

    // free-fly カメラ / ギズモ / EditorMode / クリア監視の編集ロジックを先に回す
    m_controller->Tick();
    HandleModeToggleInput(*m_controller);
    HandlePauseInput(*m_controller);
    HandleUiVisibilityInput(*m_controller);
}

void EditorLayer::OnRender()
{
    if (!IsActive() || !m_controller || !m_imgui)
        return;
    LevelEditorController& editor = *m_controller;

    // ImGui の 1 フレームを Layer が囲う。Renderer::BeginFrame 済の RT へ EndFrame の Render が描く
    m_imgui->BeginFrame();

    // ギズモ / palette / 編集ビジュアルといった編集用の上乗せ描画と debug provenance 退避
    editor.Render();

    // 終了確認は UI 非表示やプレイ中でも必ず出すため m_uiVisible のゲート外で描く
    RenderQuitModal(editor);

    // プレイ中は F5 でエディタ UI を丸ごと隠せる。 隠している間も 3D 描画とゲーム進行はそのまま走る
    if (m_uiVisible)
    {
        // Hierarchy / Inspector はプレイ中も出す。 Player / Camera を選んで操作感をライブ調整できるようにするため
        // DockSpace も両モードで毎フレーム置き、 edit で組んだドッキングがプレイ移行で崩れないようにする
        RenderDockSpaceHost();
        if (editor.CurrentMode() == LevelEditorController::Mode::Edit)
        {
            editor.Editor().RenderFileBrowser();
            RenderToolModePanel(editor);
            RenderMaterialsPanel(editor);
        }
        else if (editor.Play().paused)
            RenderPauseModal(editor);

        RenderHierarchyPanel(editor);
        RenderInspectorPanel(editor);

        RenderFpsOverlay();
        RenderRenderSettingsPanel(editor);
    }

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

void EditorLayer::HandleUiVisibilityInput(LevelEditorController& editor) noexcept
{
    // 編集モードでは UI を常時表示に戻す。 トグルはプレイ中だけ効かせる
    if (editor.CurrentMode() != LevelEditorController::Mode::Play)
    {
        m_uiVisible = true;
        return;
    }

    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;
    auto& input = app->Input();

    // 隠している間は ImGui がキーボードを掴まないので F5 で再表示できる
    if (!input.UiWantsKeyboard() && input.Keyboard().IsPressed(NS::Platform::Key::F5))
        m_uiVisible = !m_uiVisible;
}

void EditorLayer::RenderDockSpaceHost() noexcept
{
#if NS_EDITOR_ENABLED
    // 中央ノードは背景非描画 + 入力素通しで透過なので、 奥の全画面 3D とギズモがそのまま見え
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
    // 固定座標を置くとドッキング配置と競合するので位置はドッキング / imgui.ini 任せ
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
        // 編集カメラは配置物ではないが、 選んで Inspector に出せるよう先頭に常設する
        // プレイヤーは objects の実体になったので下の一覧に "Player" として並ぶ
        if (ImGui::Selectable("Camera", editor.IsCameraSelected()))
            editor.SelectCamera();
        ImGui::Separator();

        const auto& objects = editor.Level().objects;
        const std::size_t selected = editor.SelectedObjectIndex();

        ImGui::Text("%zu objects", objects.size());
        ImGui::Separator();

        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            const NS::Game::Level::ObjectInstance& object = objects[i];
            const bool grid = (object.flags & NS::Game::Level::kObjectFlagGridAligned) != 0;
            const char* name = NS::Game::Blocks::ObjectDisplayName(object);

            char label[96];
            std::snprintf(label, sizeof(label), "[%zu] %s (%s)", i, name, grid ? "grid" : "free");

            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(label, i == selected))
                editor.SelectObjectByIndex(i);
            ImGui::PopID();
        }

        if (objects.empty())
            ImGui::TextDisabled("(no objects)");

        if (ImGui::SmallButton("+ Add Object"))
            editor.AddObject();
        ImGui::SameLine();
        // 据え置きカメラも通常の配置物。 上の objects 一覧に "Camera" として並び、 選択・変形・削除も共通
        if (ImGui::SmallButton("+ Add Camera"))
            editor.AddCameraObject();
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
        // ObjectRef フィールドの参照先候補。 Hierarchy と同じ並びと表示名で全配置物を出す
        std::vector<NS::Editor::ObjectRefOption> refOptions;
        refOptions.reserve(editor.Level().objects.size());
        for (std::size_t i = 0; i < editor.Level().objects.size(); ++i)
        {
            const NS::Game::Level::ObjectInstance& candidate = editor.Level().objects[i];
            char label[96];
            std::snprintf(label,
                          sizeof(label),
                          "[%zu] %s (id %u)",
                          i,
                          NS::Game::Blocks::ObjectDisplayName(candidate),
                          candidate.objectId);
            refOptions.push_back(NS::Editor::ObjectRefOption{candidate.objectId, label});
        }

        if (editor.IsCameraSelected())
        {
            ImGui::Text("Camera");
            ImGui::Separator();
            // ブレンド秒の Brain と編集中=free-fly / プレイ中=follow の現在 active な vcam を反射で出す
            // 値はライブで効き保存はしない
            auto* brain = editor.CameraBrainObject();
            auto* vcam = editor.ActiveVirtualCameraObject();
            if (brain == nullptr && vcam == nullptr)
                ImGui::TextDisabled("(no camera)");
            if (brain != nullptr)
                (void)NS::Editor::DrawObjectComponents(*brain, refOptions);
            if (vcam != nullptr && vcam != brain)
                (void)NS::Editor::DrawObjectComponents(*vcam, refOptions);

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
                    NS::Game::Blocks::ObjectDisplayName(obj),
                    grid ? "grid" : "free");
        ImGui::Separator();

        if (grid)
        {
            ImGui::Text("Cell: (%d, %d, %d)",
                        static_cast<int>(NS::Game::Level::ObjectCellX(obj)),
                        static_cast<int>(NS::Game::Level::ObjectCellY(obj)),
                        static_cast<int>(NS::Game::Level::ObjectCellZ(obj)));
            if (NS::Game::Blocks::IsGridSolidObject(obj))
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
            // free オブジェクトは runtime Transform が真実の源なので即反映し、 SyncFreeObjectTransforms が永続化する
            ImGui::SeparatorText("Transform");

            float pos[3] = {obj.positionX, obj.positionY, obj.positionZ};
            if (ImGui::DragFloat3("Position", pos, 0.05f))
                editor.SetSelectedFreePosition(NS::Math::Vector3{pos[0], pos[1], pos[2]});
            if (ImGui::IsItemActivated())
                editor.BeginTransformEdit();
            if (ImGui::IsItemDeactivatedAfterEdit())
                editor.CommitTransformEdit();

            // 回転は内部 quaternion を度の Euler に直して編集し、 入力を quaternion へ戻す
            // 滑らかに回し続けるならギズモ R が向く。 ここは角度の直接入力 / 微調整用
            const NS::Math::Quaternion q{obj.rotationX, obj.rotationY, obj.rotationZ, obj.rotationW};
            const NS::Math::Vector3 euler = q.ToEuler();
            float rot[3] = {NS::Math::RadiansToDegrees(euler.x),
                            NS::Math::RadiansToDegrees(euler.y),
                            NS::Math::RadiansToDegrees(euler.z)};
            if (ImGui::DragFloat3("Rotation", rot, 0.5f))
                editor.SetSelectedFreeRotation(NS::Math::Quaternion::CreateFromYawPitchRoll(
                    NS::Math::Vector3{NS::Math::DegreesToRadians(rot[0]),
                                      NS::Math::DegreesToRadians(rot[1]),
                                      NS::Math::DegreesToRadians(rot[2])}));
            if (ImGui::IsItemActivated())
                editor.BeginTransformEdit();
            if (ImGui::IsItemDeactivatedAfterEdit())
                editor.CommitTransformEdit();

            float scl[3] = {obj.scaleX, obj.scaleY, obj.scaleZ};
            if (ImGui::DragFloat3("Scale", scl, 0.05f))
                editor.SetSelectedFreeScale(NS::Math::Vector3{scl[0], scl[1], scl[2]});
            if (ImGui::IsItemActivated())
                editor.BeginTransformEdit();
            if (ImGui::IsItemDeactivatedAfterEdit())
                editor.CommitTransformEdit();
        }

        ImGui::Separator();
        // 材質の適用は Assets パネルのドロップ / クリック。 ここでは現在値の表示のみ
        if (obj.materialIndex >= 0 && static_cast<std::size_t>(obj.materialIndex) < editor.Level().materialPaths.size())
            ImGui::Text("Material: %s",
                        editor.Level().materialPaths[static_cast<std::size_t>(obj.materialIndex)].c_str());
        else
            ImGui::TextDisabled("Material: default");

        // 選択オブジェクトの runtime Component を反射で一覧編集する
        // 編集後に全コンポーネントを components データへ書き戻して保存・rebuild に乗せる
        if (auto* go = editor.SelectedObjectGameObject())
        {
            ImGui::Separator();
            if (NS::Editor::DrawObjectComponents(*go, refOptions))
                editor.SyncSelectedObjectComponentsFromComponent();
        }

        // 自由オブジェクトはコンポーネント構成をデータとして編集できる
        // 反射編集は上の一覧、 ここは構成そのものの 追加 / 複製 / コピー / 削除 を担う
        if (!grid)
        {
            ImGui::SeparatorText("Components");

            // curated 一覧から末尾へ足す。 選べる型は登録済みに限られ未知型は生成できない
            if (ImGui::Button("+ Add Component"))
                ImGui::OpenPopup("AddComponentPopup");
            if (ImGui::BeginPopup("AddComponentPopup"))
            {
                for (const std::string& name : NS::Scene::RegisteredNames())
                {
                    // プレイヤーの印である入力 component は手で足させない。 プレイヤーは常に 1 体で system が管理する
                    if (name == "PlayerInputComponent")
                        continue;
                    if (ImGui::Selectable(name.c_str()))
                        editor.AddComponentToSelected(name);
                }
                ImGui::EndPopup();
            }

            if (obj.components.empty())
                ImGui::TextDisabled("コンポーネント無し。 足すと表示・当たりが付く");

            // データ上のコンポーネント 1 件ずつに Copy / Delete を出す
            // 添字で狙うので同型が複数あっても選んだ 1 つだけを取り違えずに扱える
            for (std::size_t k = 0; k < obj.components.size(); ++k)
            {
                const std::string& typeName = obj.components[k].typeName;
                ImGui::PushID(static_cast<int>(k));
                ImGui::TextUnformatted(typeName.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Copy"))
                    editor.CopyComponentToClipboard(k);
                // 最後の 1 個は消すと空構成のゴーストになるので Delete を出さない
                // プレイヤーの印である入力 component も出現位置ごと壊れるため消させない
                if (obj.components.size() > 1 && typeName != "PlayerInputComponent")
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Delete"))
                        editor.RemoveComponentFromSelected(k);
                }
                if (typeName == "MeshRendererComponent")
                    ImGui::TextColored(ImVec4{1.0f, 0.6f, 0.2f, 1.0f}, "削除すると見えなくなる");
                ImGui::PopID();
            }

            if (editor.HasClipboardComponent())
            {
                if (ImGui::Button("Paste Component"))
                    editor.PasteClipboardComponentToSelected();
            }

            ImGui::Separator();
            if (editor.SelectedIsPlayerObject())
            {
                // プレイヤーは必ず 1 体なので複製の代わりに、 手触りの現在値を新規レベル用の既定テンプレートへ
                // 書き出すボタンを出す。 レベル保存とは別口
                if (ImGui::Button("既定テンプレートへ保存"))
                    if (auto* player = editor.SelectedObjectGameObject())
                        (void)NS::Editor::SavePlayerTuning(*player);
            }
            else if (ImGui::Button("Duplicate Object"))
                editor.DuplicateSelectedObject();
        }
    }
    ImGui::End();
#else
    (void)editor;
#endif
}

void EditorLayer::RenderMaterialsPanel(LevelEditorController& editor) noexcept
{
#if NS_EDITOR_ENABLED
    // 適用先のギズモ選択は Object モードにしか存在しないので Object モード専用
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
        RenderAssetTree(NS::Core::FileSystem::ContentRoot() / "Assets", editor);
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

    // サブフォルダを TreeNode で再帰表示する。 open 時のみ中身を走査する遅延読み
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

bool EditorLayer::OnQuitRequested() noexcept
{
    if (m_quitConfirmed)
        return true;
    // まだ確認していない終了要求は modal を開いて握りつぶす。 取り下げを Application に返す
    m_quitModalOpen = true;
    m_quitSaveFailed = false;
    return false;
}

void EditorLayer::RenderQuitModal(LevelEditorController& editor) noexcept
{
#if NS_EDITOR_ENABLED
    if (!m_quitModalOpen)
        return;
    const auto vp = ImGui::GetMainViewport();
    if (vp != nullptr)
    {
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                                ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
    }
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("終了の確認", nullptr, kFlags))
    {
        ImGui::TextUnformatted("変更を保存して終了しますか");
        ImGui::Separator();
        if (ImGui::Button("保存して終了", ImVec2(180.0f, 0.0f)))
        {
            // 保存成功でのみ終了する。 失敗時は modal を残しデータ消失を防ぐ
            if (editor.Editor().SaveForQuit())
            {
                m_quitConfirmed = true;
                m_quitModalOpen = false;
                NS::App::Application::Quit();
            }
            else
            {
                m_quitSaveFailed = true;
            }
        }
        if (ImGui::Button("保存せず終了", ImVec2(180.0f, 0.0f)))
        {
            m_quitConfirmed = true;
            m_quitModalOpen = false;
            NS::App::Application::Quit();
        }
        if (ImGui::Button("キャンセル", ImVec2(180.0f, 0.0f)))
            m_quitModalOpen = false;
        if (m_quitSaveFailed)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "保存に失敗しました");
    }
    ImGui::End();
#else
    (void)editor;
#endif
}
