#pragma once

#include "Editor/ActorListPanel.h"
#include "Editor/AssetsPanel.h"
#include "Editor/ConsolePanel.h"
#include "Editor/DockController.h"
#include "Editor/GameViewPanel.h"
#include "Editor/ActionTimelinePanel.h"
#include "Editor/InspectorPanel.h"
#include "Editor/QuitModal.h"
#include "Editor/SceneViewPanel.h"
#include "Editor/ToolModePanel.h"
#include "NSlib/App/Layer.h"
#include "NSlib/Core/Windows/InputReader.h"

#include <memory>

namespace NS
{
    namespace UI
    {
        class ImGuiContext;
    }
} // namespace NS

class LevelEditorController;

//! ゲームの更新・描画に編集用の UI と自由視点を重ねるレイヤー
class Editor : public NS::Layer
{
public:
    Editor();
    ~Editor() override;

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate() override;
    void OnRender() override;

private:
    static void HandleModeToggleInput(LevelEditorController& editor, const NS::OS::InputPresses& presses) noexcept;
    static void HandlePauseInput(LevelEditorController& editor, const NS::OS::InputPresses& presses) noexcept;

    //! エディタ UI 全体の表示・非表示を切り替える入力を見る
    void HandleUiVisibilityInput(LevelEditorController& editor, const NS::OS::InputPresses& presses) noexcept;

    //! 画面最上部に File / Edit / SceneActor 等のメインメニューバーを描画する
    //! 帯の高さ分だけビューポート作業領域が下がるので、後続のツールバー / ドックは自動でずれる
    void RenderMainMenuBar(LevelEditorController& editor) noexcept;

    //! 画面最上部にプレイ制御ツールバーの帯を描画する
    //! @return 帯の高さ。ドックホストはこの分だけ上端を空ける
    [[nodiscard]] static float RenderPlayToolbar(LevelEditorController& editor) noexcept;

    //! 全面化中のパネルだけをワークエリア全面へ描く。ドックの配置は別途維持する
    void RenderMaximizedPanel(LevelEditorController& editor, float topOffset) noexcept;

    //! 編集モードのキー操作を処理する。Delete = 削除、Ctrl+D = 複製、F = 選択物へ寄る
    //! 押した瞬間は OnUpdate が固定ステップごとに読むので、描画からでなく更新から呼ぶ
    static void HandleEditShortcuts(LevelEditorController& editor, const NS::OS::InputPresses& presses) noexcept;

    std::unique_ptr<NS::UI::ImGuiContext> m_imgui;
    std::unique_ptr<LevelEditorController> m_controller;
    NS::OS::InputReader m_inputReader; // OnUpdate の頭で固定ステップごとに読む

    NS::Editor::ActorListPanel m_actorList;
    NS::Editor::InspectorPanel m_inspector;
    NS::Editor::ConsolePanel m_console;
    NS::Editor::ToolModePanel m_toolMode;
    NS::Editor::AssetsPanel m_assets;
    NS::Editor::QuitModal m_quitModal;
    NS::Editor::DockController m_dock;
    NS::Editor::SceneViewPanel m_sceneView;
    NS::Editor::GameViewPanel m_gameView;
    NS::Editor::ActionTimelinePanel m_actionTimeline;

    bool m_uiVisible = true;
    bool m_lastModeWasPlay = false; // 前フレームがプレイモードだったか。タブ自動フォーカスの切替検知に使う
};
