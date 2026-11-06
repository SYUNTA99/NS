#include "Editor/EditorLayer.h"

#include "Editor/LevelEditorController.h"
#include "Editor/LevelFilePaths.h"
#include "Editor/PanelIds.h"
#include "Editor/PlayControls.h"
#include "Game/Game.h"
#include "NSlib/App/Application.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Windows/Gamepad.h"
#include "NSlib/Core/Windows/Input.h"
#include "NSlib/Core/Windows/Keyboard.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/UI/ImGuiContext.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

namespace NS
{
    namespace Editor
    {
        namespace
        {
            // メニューバーだけ明るい帯＋濃い文字にする。他のポップアップは暗いテーマ任せ
            const ImVec4 k_MenuBarBg{0.96f, 0.96f, 0.96f, 1.0f};
            const ImVec4 k_MenuBarText{0.10f, 0.10f, 0.10f, 1.0f};
        } // namespace

        EditorLayer::EditorLayer() : Layer("Editor") {}
        EditorLayer::~EditorLayer() = default;

        void EditorLayer::OnAttach()
        {
            // SceneManager::LoadScene が同期実行するので、Game レイヤが立てた空の scene は既に OnStart 済
            // 起動シーンは Setup が編集モードへ切替えた後に開く
            Application* app = Application::Get();
            Game* game = Game::Get();
            decltype(game->CurrentScene()) scene = nullptr;
            if (game != nullptr)
            {
                scene = game->CurrentScene();
            }
            if (app == nullptr || scene == nullptr)
            {
                NS_LOG_ERROR(App, "EditorLayer::OnAttach: app / play scene 不在のため編集を起動できない");
                return;
            }

            // ImGui ライフサイクルを Layer が所有する。Application は UI を知らないので editor が立ち上げる
            m_imgui = std::make_unique<UI::ImGuiContext>(app->Window(), app->Device());
            if (!m_imgui->IsValid())
            {
                NS_LOG_ERROR(App, "ImGuiContext 構築失敗、 編集 UI は機能しない");
            }
            else
            {
                ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
            }

            // Platform は UI 実装を知らないので、転送は hook 経由にする
            app->Window().SetMessageHook([imgui = m_imgui.get()](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
                if (imgui != nullptr)
                {
                    (void)imgui->ForwardWndProc(hwnd, msg, wParam, lParam);
                }
            });

            m_controller = std::make_unique<LevelEditorController>(*scene);
            m_controller->Editor().SetSessionPath(
                OS::FileSystem::Combine(OS::FileSystem::ContentRoot(), "build/editor-session.json"));
            m_controller->Setup(StartLevelName());

            // 初回フレームからオフスクリーンで描けるよう、ウィンドウサイズを初期の目標サイズにしておく
            const Size2D viewSize = app->Window().Size();
            m_sceneView.SetInitialSize(viewSize);
            m_gameView.SetInitialSize(viewSize);

            // 終了要求を握って保存確認を挟む。出荷には Editor が無いのでリリースは確認なしで終了する
            app->SetQuitGuard([this]() {
                m_controller->CommitTransformEdit();
                m_controller->CommitClassObjEdit();
                m_controller->CommitAssetEdit();
                return m_quitModal.RequestQuit(HasUnsavedWork(*m_controller, m_actionTimeline));
            });

            NS_LOG_INFO(App, "Editor attached (Debug/Dev/GameDebug only)");
        }

        void EditorLayer::OnDetach()
        {
            m_dock.ExitMaximize();
            // overlay は逆順で OnDetach されるので、scene を破棄する Game::OnDetach より先にここが走る
            if (m_controller)
            {
                // シーンの描画視点を外してから、参照先の描画先を破棄する
                m_controller->SetSceneViews({});
                m_controller->Teardown();
            }
            m_controller.reset();

            // ImGui を破棄する前に hook を外し、WndProc から無効になった context を踏まないようにする
            if (Application* app = Application::Get())
            {
                app->SetQuitGuard(nullptr);
                app->Window().SetMessageHook(nullptr);
                app->Input().SetUiCapture({});
                // D3DDevice が非所有ポインタを宙吊りにしないよう、ターゲットを破棄する前に必ず外す
                app->Device().SetSceneTarget(nullptr);
            }
            m_sceneView.ReleaseTarget();
            m_gameView.ReleaseTarget();
            // ImGui_ImplDX11_Shutdown が ID3D11Device を要求するため D3DDevice 健在で Application::Shutdown
            // より前の今に破棄
            m_imgui.reset();
            NS_LOG_INFO(App, "Editor detached");
        }

        void EditorLayer::OnUpdate()
        {
            const OS::InputPresses presses = m_inputReader.Read();
            if (!IsActive() || !m_controller)
            {
                return;
            }

            m_controller->Tick();
            HandleModeToggleInput(*m_controller, presses);
            HandlePauseInput(*m_controller, presses);
            HandleUiVisibilityInput(*m_controller, presses);
            HandleEditShortcuts(*m_controller, presses);
        }

        void EditorLayer::OnRender()
        {
            if (!IsActive() || !m_controller || !m_imgui)
            {
                return;
            }
            LevelEditorController& editor = *m_controller;
            Application* app = Application::Get();
            if (app == nullptr)
            {
                return;
            }

            // Layer が ImGui のフレームを管理する。D3DDevice::BeginFrame の描画先へ EndFrame で描く
            m_imgui->BeginFrame();

            // 編集用の ImGui の上乗せ。カーソルの枠 / パレット / ギズモ。立体の図形は Scene タブを描く時に積む
            editor.Render();

            // 終了確認は UI 非表示やプレイ中でも必ず出すため m_uiVisible のゲート外で描く
            m_quitModal.Render(editor, m_actionTimeline);

            const bool playMode = editor.CurrentMode() == LevelEditorController::Mode::Play;

            // プレイ中は F5 でエディタ UI を丸ごと隠せる。隠している間も 3D 描画とゲーム進行はそのまま走る
            if (m_uiVisible)
            {
                m_sceneView.ResetVisibility();
                m_gameView.ResetVisibility();

                // メニューバーは帯の分だけビューポート作業領域を下げるので、ツールバーより先に置いて上端を確保する
                RenderMainMenuBar(editor);
                const float toolbarHeight = RenderPlayToolbar(editor);
                m_dock.RenderDockSpaceHost(toolbarHeight);

                if (!m_dock.IsMaximized())
                {
                    // 個体一覧と Inspector はプレイ中も出す。PlayerActor / Camera
                    // を選んで操作感をライブ調整できるようにするため DockSpace も両モードで毎フレーム置き、edit
                    // で組んだドッキングがプレイ移行で崩れないようにする
                    m_sceneView.Render(editor);
                    m_gameView.Render(editor);
                    ImGui::BeginDisabled(!editor.IsSceneEditable());
                    m_toolMode.Render(editor);
                    ImGui::EndDisabled();

                    m_actorList.Render(editor);
                    m_inspector.Render(editor);
                    m_actionTimeline.Render(editor);

                    // Assets はプレイ中も出し続け Console と同じタブに重ねる
                    m_assets.Render(editor);
                    m_console.Render();

                    // モードが変わったフレームだけ前面のタブへ自動フォーカスする。Tab / ボタン / Quit to Edit
                    // のどこから 切替わっても CurrentMode の変化検知で一律に効く
                    const std::optional<CenterTab> tabFocus =
                        TabFocusOnModeChange({.wasPlayMode = m_lastModeWasPlay, .playMode = playMode});
                    if (tabFocus.has_value())
                    {
                        if (*tabFocus == CenterTab::Game)
                        {
                            ImGui::SetWindowFocus(k_PanelGame);
                        }
                        else
                        {
                            ImGui::SetWindowFocus(k_PanelScene);
                        }
                    }

                    m_dock.TickTabFocus();
                }
                else
                {
                    // ドックの配置を維持したまま、対象 1 枚をワークエリア全面へ描く
                    RenderMaximizedPanel(editor, toolbarHeight);
                }

                // 保存と読み込みの窓とショートカットは、全面化の間も効かせる
                if (editor.IsSceneEditable())
                {
                    editor.Editor().RenderFileBrowser();
                }

                // パネルの内容に重ならないショートカットで全面化と復元を切り替える
                m_dock.RenderMaximizeButton(editor.EditShortcutsBlocked());

                m_lastModeWasPlay = playMode;
            }
            else
            {
                m_dock.RenderDockSpaceHost(0.0f, true);
                // F5 の全画面直描き中はパネル矩形が無いので予備の全画面矩形へ戻し、自由視点の上書きも外す
                m_sceneView.ClearForHiddenUi(editor);
            }

            // ImGui 実描画の直前に backbuffer へ戻す。それまではオフスクリーンへ描いたままで良い
            app->Device().BindBackbuffer();
            m_imgui->EndFrame();

            // 次フレームの描画先を決める。SRV 参照 (ImGui 実描画) が終わった直後の安全な位置で切り替える
            if (m_uiVisible)
            {
                std::vector<SceneView> views;
                if (std::optional<SceneView> sceneView = m_sceneView.CollectView(editor))
                {
                    views.push_back(*sceneView);
                }
                if (std::optional<SceneView> gameView = m_gameView.CollectView(editor))
                {
                    views.push_back(*gameView);
                }
                editor.SetSceneViews(std::move(views));
            }
            else
            {
                // F5 全画面プレイ: ビュー列を空にし backbuffer へ Brain 視点で 1 回描く
                editor.SetSceneViews({});
            }
            // ビュー列方式なので単一 sceneTarget は使わない。BeginFrame には backbuffer だけ clear させる
            app->Device().SetSceneTarget(nullptr);

            m_gameView.UpdateMouseLatch();
            m_sceneView.UpdateMouseLatch();

            // 次フレームの gameplay / Window 入力ゲート用に UI キャプチャ状態を Input へ反映する
            // F5 で UI を隠している間はパネルを描かないので、焦点は無い扱いにして前の振り分けのままにする
            std::optional<CenterTab> focusedPanel;
            if (m_uiVisible)
            {
                if (m_sceneView.IsFocused())
                {
                    focusedPanel = CenterTab::Scene;
                }
                else if (m_gameView.IsFocused())
                {
                    focusedPanel = CenterTab::Game;
                }
            }
            const InputOwnership owner = ResolveInputOwnership({.playMode = playMode,
                                                                .uiWantsMouse = m_imgui->WantCaptureMouse(),
                                                                .uiWantsKeyboard = m_imgui->WantCaptureKeyboard(),
                                                                .textInput = ImGui::GetIO().WantTextInput,
                                                                .itemActive = ImGui::IsAnyItemActive(),
                                                                .modalOpen = ImGui::GetTopMostPopupModal() != nullptr,
                                                                .editSceneLatched = m_sceneView.IsMouseLatched(),
                                                                .gameLatched = m_gameView.IsMouseLatched(),
                                                                .cursorReleased = app->Window().IsCursorVisible(),
                                                                .sceneLatched = m_sceneView.IsFreeViewLatched(),
                                                                .sceneLooking = m_sceneView.IsFreeFlying(),
                                                                .focusedPanel = focusedPanel});
            app->Input().SetUiCapture({.wantMouse = owner.uiMouse,
                                       .wantKeyboard = owner.uiKeyboard,
                                       .leftButtonToGame = owner.leftButtonToGame});
            editor.SetEditShortcutsBlocked(owner.editShortcutsBlocked);

            // 見回し終了時にキー状態を解除し、操作対象の切替へ押しっぱなしを持ち越さない
            if (m_sceneView.ConsumeFreeFlyReleased())
            {
                app->Input().Keyboard().ClearState();
            }
        }

        void EditorLayer::HandleModeToggleInput(LevelEditorController& editor, const OS::InputPresses& presses) noexcept
        {
            Application* app = Application::Get();
            if (app == nullptr)
            {
                return;
            }
            OS::Input& input = app->Input();

            // UI がキーボードを握っている間は mode flip させない
            const bool wantKb = input.UiWantsKeyboard();

            const bool tabPressed = !wantKb && presses.WasPressed(OS::Key::Tab);
            const bool startPressed = input.Gamepad().IsConnected() && presses.WasPressed(OS::GamepadButton::Start);

            if (tabPressed || startPressed)
            {
                if (editor.CurrentMode() == LevelEditorController::Mode::Edit)
                {
                    editor.EnterPlay();
                }
                else
                {
                    editor.EnterEdit();
                }
            }
        }

        void EditorLayer::HandlePauseInput(LevelEditorController& editor, const OS::InputPresses& presses) noexcept
        {
            if (editor.CurrentMode() != LevelEditorController::Mode::Play)
            {
                return;
            }
            Application* app = Application::Get();
            if (app == nullptr)
            {
                return;
            }
            OS::Input& input = app->Input();

            const bool wantKb = input.UiWantsKeyboard();

            const bool pPressed = !wantKb && presses.WasPressed(OS::Key::P);
            const bool backPressed = input.Gamepad().IsConnected() && presses.WasPressed(OS::GamepadButton::Back);

            if (pPressed || backPressed)
            {
                editor.TogglePlayPause();
            }
        }

        void EditorLayer::HandleUiVisibilityInput(LevelEditorController& editor,
                                                  const OS::InputPresses& presses) noexcept
        {
            // 編集モードでは UI を常時表示に戻す。トグルはプレイ中だけ効かせる
            if (editor.CurrentMode() != LevelEditorController::Mode::Play)
            {
                m_uiVisible = true;
                return;
            }

            Application* app = Application::Get();
            if (app == nullptr)
            {
                return;
            }
            OS::Input& input = app->Input();

            // 隠している間は ImGui がキーボードを掴まないので F5 で再表示できる
            if (!input.UiWantsKeyboard() && presses.WasPressed(OS::Key::F5))
            {
                m_uiVisible = !m_uiVisible;
            }
        }

        float EditorLayer::RenderPlayToolbar(LevelEditorController& editor) noexcept
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            const float height = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;

            const ImGuiViewport* vp = ImGui::GetMainViewport();
            if (vp == nullptr)
            {
                return height;
            }
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(ImVec2{vp->WorkSize.x, height});
            constexpr ImGuiWindowFlags k_ToolbarFlags =
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;
            const bool playMode = editor.CurrentMode() == LevelEditorController::Mode::Play;
            if (ImGui::Begin("##PlayToolbar", nullptr, k_ToolbarFlags))
            {
                // レベル名と未保存の表示 (*) はアクターの一覧の見出しに出すので、この帯は再生操作だけを中央に置く
                const PlayToolbarState state =
                    MakePlayToolbarState({.playMode = playMode, .paused = editor.PlayPaused()});
                const float buttonWidth = ImGui::GetFrameHeight() * 1.6f;
                const float totalWidth = buttonWidth * 3.0f + style.ItemSpacing.x * 2.0f;
                ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetWindowSize().x - totalWidth) * 0.5f));

                if (state.playActive)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
                }
                const char* playLabel = "▶";
                if (state.playActive)
                {
                    playLabel = "■";
                }
                if (ImGui::Button(playLabel, ImVec2(buttonWidth, 0.0f)))
                {
                    if (state.playActive)
                    {
                        editor.EnterEdit();
                    }
                    else
                    {
                        editor.EnterPlay();
                    }
                }
                if (state.playActive)
                {
                    ImGui::PopStyleColor();
                }

                ImGui::SameLine();
                ImGui::BeginDisabled(!state.pauseEnabled);
                if (state.pauseDown)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
                }
                if (ImGui::Button("||", ImVec2(buttonWidth, 0.0f)))
                {
                    editor.TogglePlayPause();
                }
                if (state.pauseDown)
                {
                    ImGui::PopStyleColor();
                }
                ImGui::EndDisabled();

                ImGui::SameLine();
                ImGui::BeginDisabled(!state.stepEnabled);
                if (ImGui::Button("▶|", ImVec2(buttonWidth, 0.0f)))
                {
                    editor.RequestStepFrame();
                }
                ImGui::EndDisabled();
            }
            ImGui::End();
            return height;
        }

        void EditorLayer::RenderMainMenuBar(LevelEditorController& editor) noexcept
        {
            EditorMode& ed = editor.Editor();
            const bool editEnabled = editor.IsSceneEditable();

            // 縦の余白を詰めてバーを細くする。横は掴みやすさのため残す
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{9.0f, 1.0f});
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{10.0f, 4.0f});
            ImGui::PushStyleColor(ImGuiCol_MenuBarBg, k_MenuBarBg);
            ImGui::PushStyleColor(ImGuiCol_Text, k_MenuBarText);
            if (ImGui::BeginMainMenuBar())
            {
                // ドロップダウンだけ白地・黒文字にする。バーのラベルは元の配色のまま
                ImGui::PushStyleColor(ImGuiCol_PopupBg, k_MenuBarBg);
                if (ImGui::BeginMenu("File"))
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, k_MenuBarText);
                    if (ImGui::MenuItem("保存", "Ctrl+S", false, editEnabled))
                    {
                        ed.RequestSave();
                    }
                    if (ImGui::MenuItem("名前を付けて保存...", "Ctrl+Shift+S", false, editEnabled))
                    {
                        ed.OpenSaveModal();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("終了", "Alt+F4"))
                    {
                        Application::Quit();
                    }
                    ImGui::PopStyleColor();
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Edit"))
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, k_MenuBarText);
                    if (ImGui::MenuItem("元に戻す", "Ctrl+Z", false, editEnabled && ed.CanUndo()))
                    {
                        ed.PerformUndo();
                    }
                    if (ImGui::MenuItem("やり直す", "Ctrl+Y", false, editEnabled && ed.CanRedo()))
                    {
                        ed.PerformRedo();
                    }
                    ImGui::PopStyleColor();
                    ImGui::EndMenu();
                }
                m_dock.RenderWindowMenu();
                ImGui::PopStyleColor();

                ImGui::EndMainMenuBar();
            }
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        }

        void EditorLayer::RenderMaximizedPanel(LevelEditorController& editor, float topOffset) noexcept
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            if (vp == nullptr)
            {
                return;
            }
            const ImVec2 hostPos{vp->WorkPos.x, vp->WorkPos.y + topOffset};
            const ImVec2 hostSize{vp->WorkSize.x, vp->WorkSize.y - topOffset};

            // ドックから外し、ワークエリア全面へ固定する。SetNext* は次の Begin にだけ効く
            ImGui::SetNextWindowPos(hostPos, ImGuiCond_Always);
            ImGui::SetNextWindowViewport(vp->ID);
            ImGui::SetNextWindowSize(hostSize, ImGuiCond_Always);
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always);

            const std::string& name = m_dock.MaximizedPanel();
            if (name == k_PanelScene)
            {
                m_gameView.Suppress(editor);
                m_sceneView.Render(editor);
                return;
            }
            if (name == k_PanelGame)
            {
                m_sceneView.Suppress(editor);
                m_gameView.Render(editor);
                return;
            }

            // 中央以外は映像を持たないので、前面の矩形を無効化してから対象パネルを描く
            m_sceneView.Suppress(editor);
            m_gameView.Suppress(editor);
            if (name == k_PanelActorList)
            {
                m_actorList.Render(editor);
            }
            else if (name == k_PanelInspector)
            {
                m_inspector.Render(editor);
            }
            else if (name == k_PanelConsole)
            {
                m_console.Render();
            }
            else if (name == k_PanelAssets)
            {
                m_assets.Render(editor);
            }
            else if (name == k_PanelEditMode)
            {
                ImGui::BeginDisabled(!editor.IsSceneEditable());
                m_toolMode.Render(editor);
                ImGui::EndDisabled();
            }
            else if (name == k_PanelActionTimeline)
            {
                m_actionTimeline.Render(editor);
            }
            else
            {
                m_dock.ClearMaximized();
            }
        }

        void EditorLayer::HandleEditShortcuts(LevelEditorController& editor, const OS::InputPresses& presses) noexcept
        {
            if (!editor.IsSceneEditable() || editor.EditShortcutsBlocked())
            {
                return;
            }
            Application* app = Application::Get();
            if (app == nullptr)
            {
                return;
            }

            const OS::Keyboard& keyboard = app->Input().Keyboard();
            const bool ctrl = keyboard.IsHeld(OS::Key::Ctrl);
            if (presses.WasPressed(OS::Key::Delete))
            {
                editor.DeleteSelectedActor();
            }
            if (ctrl && presses.WasPressed(OS::Key::D))
            {
                editor.DuplicateSelectedActor();
            }
            if (!ctrl && presses.WasPressed(OS::Key::F))
            {
                editor.FocusSelectedInView();
            }
        }
    } // namespace Editor
} // namespace NS
