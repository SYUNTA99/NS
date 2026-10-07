#include "Editor/Editor.h"

#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "Editor/PlayControls.h"
#include "Game/Game.h"
#include "NSlib/App/Application.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/UI/ImGuiContext.h"
#include "NSlib/Windows/Gamepad.h"
#include "NSlib/Windows/Input.h"
#include "NSlib/Windows/Keyboard.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

#if NS_EDITOR_ENABLED
namespace
{
    // パネル名定数は PanelIds.h (NS::Editor) が持つ。ドック・ 全面化・ 中央ビュー・ 各パネルが共有する
    using NS::Editor::k_PanelAssets;
    using NS::Editor::k_PanelConsole;
    using NS::Editor::k_PanelEditMode;
    using NS::Editor::k_PanelGame;
    using NS::Editor::k_PanelHierarchy;
    using NS::Editor::k_PanelInspector;
    using NS::Editor::k_PanelScene;

    // メニューバーだけ明るい帯＋濃い文字にする。他のポップアップは暗いテーマ任せ
    const ImVec4 k_MenuBarBg{0.96f, 0.96f, 0.96f, 1.0f};
    const ImVec4 k_MenuBarText{0.10f, 0.10f, 0.10f, 1.0f};
} // namespace
#endif

Editor::Editor() : NS::Layer("Editor") {}
Editor::~Editor() = default;

void Editor::OnAttach()
{
    // SceneManager::LoadScene が同期実行するので起動 scene は Game レイヤが既に load + OnStart 済
    NS::Application* app = NS::Application::Get();
    Game* game = Game::Get();
    decltype(game->CurrentScene()) scene = nullptr;
    if (game != nullptr)
    {
        scene = game->CurrentScene();
    }
    if (app == nullptr || scene == nullptr)
    {
        NS_LOG_ERROR(App, "Editor::OnAttach: app / play scene 不在のため編集を起動できない");
        return;
    }

    // ImGui ライフサイクルを Layer が所有する。Application は UI を知らないので editor が立ち上げる
    m_imgui = std::make_unique<NS::UI::ImGuiContext>(app->Window(), app->Renderer());
    if (!m_imgui->IsValid())
    {
        NS_LOG_ERROR(App, "ImGuiContext 構築失敗、 編集 UI は機能しない");
    }

    // Platform は UI 実装を知らないので、転送は hook 経由にする
    app->Window().SetMessageHook([imgui = m_imgui.get()](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (imgui != nullptr)
        {
            (void)imgui->ForwardWndProc(hwnd, msg, wParam, lParam);
        }
    });

    m_controller = std::make_unique<LevelEditorController>(scene);
    m_controller->Setup(m_imgui.get());

    // 初回フレームからオフスクリーンで描けるよう、ウィンドウサイズを初期の目標サイズにしておく
    const NS::Size2D viewSize = app->Window().Size();
    m_sceneView.SetInitialSize(viewSize);
    m_gameView.SetInitialSize(viewSize);

    // 終了要求を握って保存確認を挟む。出荷には Editor が無いのでリリースは確認なしで終了する
    app->SetQuitGuard([this]() { return m_quitModal.RequestQuit(); });

    NS_LOG_INFO(App, "Editor attached (Debug/Dev/GameDebug only)");
}

void Editor::OnDetach()
{
    m_hitTimeline.ResetPreview();
    // overlay は逆順で OnDetach されるので、scene を破棄する Game::OnDetach より先にここが走る
    if (m_controller)
    {
        // world が抱えるビュー列を空にしてから、その参照先ターゲットを破棄する
        m_controller->SetSceneViews({});
        m_controller->Teardown();
    }
    m_controller.reset();

    // ImGui を破棄する前に hook を外し、WndProc から無効になった context を踏まないようにする
    if (NS::Application* app = NS::Application::Get())
    {
        app->SetQuitGuard(nullptr);
        app->Window().SetMessageHook(nullptr);
        app->Input().SetUiCapture({});
        // Renderer が非所有ポインタを宙吊りにしないよう、ターゲットを破棄する前に必ず外す
        app->Renderer().SetSceneTarget(nullptr);
    }
    m_sceneView.ReleaseTarget();
    m_gameView.ReleaseTarget();
    // ImGui_ImplDX11_Shutdown が ID3D11Device を要求するため Renderer 健在で Application::Shutdown より前の今に破棄
    m_imgui.reset();
    NS_LOG_INFO(App, "Editor detached");
}

void Editor::OnUpdate()
{
    if (!IsActive() || !m_controller)
    {
        return;
    }

    m_controller->Tick();
    HandleModeToggleInput(*m_controller);
    HandlePauseInput(*m_controller);
    HandleUiVisibilityInput(*m_controller);
}

void Editor::OnRender()
{
    if (!IsActive() || !m_controller || !m_imgui)
    {
        return;
    }
    LevelEditorController& editor = *m_controller;
    NS::Application* app = NS::Application::Get();
    if (app == nullptr)
    {
        return;
    }

    // ImGui の 1 フレームを Layer が囲う。Renderer::BeginFrame 済の RT へ EndFrame の Render が描く
    m_imgui->BeginFrame();

    // 編集用の ImGui の上乗せ。カーソルの枠 / パレット / ギズモ。立体の図形は Scene タブを描く時に積む
    editor.Render();

    // 終了確認は UI 非表示やプレイ中でも必ず出すため m_uiVisible のゲート外で描く
    m_quitModal.Render(editor);

    const bool playMode = editor.CurrentMode() == LevelEditorController::Mode::Play;

    m_hitTimeline.Tick(editor, ImGui::GetIO().DeltaTime);

    // プレイ中は F5 でエディタ UI を丸ごと隠せる。隠している間も 3D 描画とゲーム進行はそのまま走る
    if (m_uiVisible)
    {
        m_sceneView.ResetVisibility();
        m_gameView.ResetVisibility();

        // メニューバーは帯の分だけビューポート作業領域を下げるので、ツールバーより先に置いて上端を確保する
        RenderMainMenuBar(editor);
        const float toolbarHeight = RenderPlayToolbar(editor);

        if (!m_dock.IsMaximized())
        {
            // Hierarchy / Inspector はプレイ中も出す。Player / Camera を選んで操作感をライブ調整できるようにするため
            // DockSpace も両モードで毎フレーム置き、edit で組んだドッキングがプレイ移行で崩れないようにする
            m_dock.RenderDockSpaceHost(toolbarHeight);
            m_sceneView.Render(editor);
            m_gameView.Render(editor);
            if (!playMode)
            {
                editor.Editor().RenderFileBrowser();
                m_toolMode.Render(editor);
            }

            m_hierarchy.Render(editor);
            m_inspector.Render(editor);
            m_hitTimeline.Render(editor);
            HandleEditShortcuts(editor);

            // Assets はプレイ中も出し続け Console と同じタブに重ねる
            m_assets.Render(editor);
            m_console.Render();

            // モードが変わったフレームだけ前面のタブへ自動フォーカスする。Tab / ボタン / Quit to Edit のどこから
            // 切替わっても CurrentMode の変化検知で一律に効く
            const std::optional<NS::Editor::CenterTab> tabFocus =
                NS::Editor::TabFocusOnModeChange({.wasPlayMode = m_lastModeWasPlay, .playMode = playMode});
            if (tabFocus.has_value())
            {
                if (*tabFocus == NS::Editor::CenterTab::Game)
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
            // 全面化中はドックも他パネルも発行せず、対象 1 枚だけをワークエリア全面へ描く
            RenderMaximizedPanel(editor, toolbarHeight);
        }

        // どちらの表示でもパネル右上に全面化 / 復元ボタンを重ねる
        m_dock.RenderMaximizeButton();

        m_lastModeWasPlay = playMode;
    }
    else
    {
        // F5 の全画面直描き中はパネル矩形が無いので予備の全画面矩形へ戻し、自由視点の上書きも外す
        m_sceneView.ClearForHiddenUi(editor);
    }

    NS::Editor::TimelinePreview* preview = nullptr;
    // 貼っている描画先を今の再生位置へ描き直す。リサイズは画像の実描画後
    if (m_uiVisible)
    {
        preview = m_hitTimeline.Preview(editor);
        if (preview != nullptr)
        {
            (void)m_gameView.RenderPreview(*preview);
        }
    }

    // ImGui 実描画の直前に backbuffer へ戻す。それまではオフスクリーンへ描いたままで良い
    app->Renderer().BindBackbuffer();
    m_imgui->EndFrame();

    // 次フレームの描画先を決める。SRV 参照 (ImGui 実描画) が終わった直後の安全な位置で切り替える
    if (m_uiVisible)
    {
        std::vector<NS::Obj::SceneView> views;
        if (std::optional<NS::Obj::SceneView> sceneView = m_sceneView.CollectView(editor))
        {
            views.push_back(*sceneView);
        }
        if (std::optional<NS::Obj::SceneView> gameView = m_gameView.CollectView(editor, preview != nullptr))
        {
            if (preview == nullptr)
            {
                views.push_back(*gameView);
            }
        }
        editor.SetSceneViews(std::move(views));
    }
    else
    {
        // F5 全画面プレイ: ビュー列を空にし backbuffer へ Brain 視点で 1 回描く
        editor.SetSceneViews({});
    }
    // ビュー列方式なので単一 sceneTarget は使わない。BeginFrame には backbuffer だけ clear させる
    app->Renderer().SetSceneTarget(nullptr);

    m_gameView.UpdateMouseLatch();
    m_sceneView.UpdateMouseLatch();

    // 次フレームの gameplay / Window 入力ゲート用に UI キャプチャ状態を Input へ反映する
    // F5 で UI を隠している間はパネルを描かないので、焦点は無い扱いにして前の振り分けのままにする
    std::optional<NS::Editor::CenterTab> focusedPanel;
    if (m_uiVisible)
    {
        if (m_sceneView.IsFocused())
        {
            focusedPanel = NS::Editor::CenterTab::Scene;
        }
        else if (m_gameView.IsFocused())
        {
            focusedPanel = NS::Editor::CenterTab::Game;
        }
    }
    const NS::Editor::InputOwnership owner =
        NS::Editor::ResolveInputOwnership({.playMode = playMode,
                                           .uiWantsMouse = m_imgui->WantCaptureMouse(),
                                           .uiWantsKeyboard = m_imgui->WantCaptureKeyboard(),
                                           .textInput = ImGui::GetIO().WantTextInput,
                                           .editSceneLatched = m_sceneView.IsMouseLatched(),
                                           .gameLatched = m_gameView.IsMouseLatched(),
                                           .cursorReleased = app->Window().IsCursorVisible(),
                                           .sceneLatched = m_sceneView.IsFreeViewLatched(),
                                           .sceneLooking = m_sceneView.IsFreeFlying(),
                                           .focusedPanel = focusedPanel});
    app->Input().SetUiCapture(
        {.wantMouse = owner.uiMouse, .wantKeyboard = owner.uiKeyboard, .leftButtonToGame = owner.leftButtonToGame});

    // 見回しドラッグの立ち下がりで押しっぱなしのキーが残らないよう解除する。WM_KEYUP も UI 捕捉中は届かない
    if (m_sceneView.ConsumeFreeFlyReleased())
    {
        app->Input().Keyboard().ClearState();
    }
}

void Editor::HandleModeToggleInput(LevelEditorController& editor) noexcept
{
    NS::Application* app = NS::Application::Get();
    if (app == nullptr)
    {
        return;
    }
    NS::OS::Input& input = app->Input();

    // UI がキーボードを握っている間は mode flip させない
    const bool wantKb = input.UiWantsKeyboard();

    const bool tabPressed = !wantKb && input.Keyboard().IsPressed(NS::OS::Key::Tab);
    const bool startPressed = input.Gamepad().IsConnected() && input.Gamepad().IsPressed(NS::OS::GamepadButton::Start);

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

void Editor::HandlePauseInput(LevelEditorController& editor) noexcept
{
    if (editor.CurrentMode() != LevelEditorController::Mode::Play)
    {
        return;
    }
    NS::Application* app = NS::Application::Get();
    if (app == nullptr)
    {
        return;
    }
    NS::OS::Input& input = app->Input();

    const bool wantKb = input.UiWantsKeyboard();

    const bool pPressed = !wantKb && input.Keyboard().IsPressed(NS::OS::Key::P);
    const bool backPressed = input.Gamepad().IsConnected() && input.Gamepad().IsPressed(NS::OS::GamepadButton::Back);

    if (pPressed || backPressed)
    {
        editor.TogglePlayPause();
    }
}

void Editor::HandleUiVisibilityInput(LevelEditorController& editor) noexcept
{
    // 編集モードでは UI を常時表示に戻す。トグルはプレイ中だけ効かせる
    if (editor.CurrentMode() != LevelEditorController::Mode::Play)
    {
        m_uiVisible = true;
        return;
    }

    NS::Application* app = NS::Application::Get();
    if (app == nullptr)
    {
        return;
    }
    NS::OS::Input& input = app->Input();

    // 隠している間は ImGui がキーボードを掴まないので F5 で再表示できる
    if (!input.UiWantsKeyboard() && input.Keyboard().IsPressed(NS::OS::Key::F5))
    {
        m_uiVisible = !m_uiVisible;
    }
}

float Editor::RenderPlayToolbar(LevelEditorController& editor) noexcept
{
#if NS_EDITOR_ENABLED
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    if (vp == nullptr)
    {
        return height;
    }
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2{vp->WorkSize.x, height});
    constexpr ImGuiWindowFlags k_ToolbarFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                                ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                                                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;
    const bool playMode = editor.CurrentMode() == LevelEditorController::Mode::Play;
    if (ImGui::Begin("##PlayToolbar", nullptr, k_ToolbarFlags))
    {
        // レベル名と未保存の印はメニューバー右端に出すので、この帯は再生操作だけを中央に置く
        const NS::Editor::PlayToolbarState state =
            NS::Editor::MakePlayToolbarState({.playMode = playMode, .paused = editor.PlayPaused()});
        const float buttonWidth = ImGui::GetFrameHeight() * 1.6f;
        const float totalWidth = buttonWidth * 3.0f + style.ItemSpacing.x * 2.0f;
        ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetWindowSize().x - totalWidth) * 0.5f));

        // 開始 / 停止トグル。編集中は ▶ で再生開始、再生中は ■ で実行から抜ける
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

        // 一時停止 / 再開。抜けずに時間だけ止める
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

        // コマ送り。一時停止したまま 1 fixed step だけ進める
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
#else
    (void)editor;
    return 0.0f;
#endif
}

void Editor::RenderMainMenuBar(LevelEditorController& editor) noexcept
{
#if NS_EDITOR_ENABLED
    NS::Editor::EditorMode& ed = editor.Editor();
    const bool playMode = editor.CurrentMode() == LevelEditorController::Mode::Play;
    const bool editEnabled = !playMode; // プレイ中はレベルへの編集を止め、進行中のセッションを壊さない

    // 縦の余白を詰めてバーを細くする。横は掴みやすさのため残す
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{9.0f, 1.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{10.0f, 4.0f});
    // ドロップダウンと同じ配色に揃え、バーも明るい背景＋濃い文字にする
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
            if (ImGui::MenuItem("レベルを開く...", "Ctrl+O", false, editEnabled))
            {
                ed.OpenLoadModal();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("終了", "Alt+F4"))
            {
                NS::Application::Quit();
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
        ImGui::PopStyleColor();

        ImGui::EndMainMenuBar();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
#else
    (void)editor;
#endif
}

void Editor::RenderMaximizedPanel(LevelEditorController& editor, float topOffset) noexcept
{
#if NS_EDITOR_ENABLED
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    if (vp == nullptr)
    {
        return;
    }
    const ImVec2 hostPos{vp->WorkPos.x, vp->WorkPos.y + topOffset};
    const ImVec2 hostSize{vp->WorkSize.x, vp->WorkSize.y - topOffset};

    // ドックから外し、ワークエリア全面へ固定する。SetNext* は次の Begin にだけ効く
    ImGui::SetNextWindowPos(hostPos, ImGuiCond_Always);
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
    if (name == k_PanelHierarchy)
    {
        m_hierarchy.Render(editor);
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
        m_toolMode.Render(editor);
    }
    else
    {
        m_dock.ClearMaximized(); // 未知名は保険で解除
    }
#else
    (void)editor;
    (void)topOffset;
#endif
}

void Editor::HandleEditShortcuts(LevelEditorController& editor) noexcept
{
#if NS_EDITOR_ENABLED
    // プレイ中の配置物は live がそのまま保存対象なので、誤爆で消さないよう編集中だけ効かせる
    if (editor.CurrentMode() != LevelEditorController::Mode::Edit)
    {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    // 改名の入力欄に居る間は Delete も D も文字入力
    if (io.WantTextInput)
    {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
    {
        editor.DeleteSelectedObject();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
    {
        editor.DuplicateSelectedObject();
    }
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
    {
        editor.FocusSelectedInView();
    }
#else
    (void)editor;
#endif
}
