#pragma once

#include "Editor/AssetsPanel.h"
#include "Editor/ConsolePanel.h"
#include "Editor/DockController.h"
#include "Editor/GameViewPanel.h"
#include "Editor/HierarchyPanel.h"
#include "Editor/InspectorPanel.h"
#include "Editor/QuitModal.h"
#include "Editor/SceneViewPanel.h"
#include "Editor/ToolModePanel.h"
#include "Runtime/App/Layer.h"

#include <memory>

namespace NS::UI
{
    class ImGuiContext;
}

class LevelEditorController;

//! @brief エディタ用のUIを提供するレイヤー
//! @details
//! ゲームを止めずに、UI パネル・ギズモ・自由視点カメラを world の上へ重ねる
class Editor : public NS::App::Layer
{
public:
    Editor();
    ~Editor() override;

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate() override;
    void OnRender() override;

private:
    static void HandleModeToggleInput(LevelEditorController& editor) noexcept;
    static void HandlePauseInput(LevelEditorController& editor) noexcept;

    //! エディタ UI 全体の表示・非表示を切り替える入力を見る
    void HandleUiVisibilityInput(LevelEditorController& editor) noexcept;

    //! @brief 画面最上部に File / Edit / GameObject 等のメインメニューバーを描画する
    //! @details 帯の高さ分だけビューポート作業領域が下がるので、 後続のツールバー / ドックは自動でずれる
    void RenderMainMenuBar(LevelEditorController& editor) noexcept;

    //! @brief 画面最上部にプレイ制御ツールバーの帯を描画する
    //! @return 帯の高さ。ドックホストはこの分だけ上端を空ける
    [[nodiscard]] static float RenderPlayToolbar(LevelEditorController& editor) noexcept;

    //! 全面化中のパネルだけをワークエリア全面へ描く。ドックと他パネルは発行しない
    void RenderMaximizedPanel(LevelEditorController& editor, float topOffset) noexcept;

    //! 編集モードのキー操作を処理する。Delete = 削除、Ctrl+D = 複製、F = 選択物へ寄る
    static void HandleEditShortcuts(LevelEditorController& editor) noexcept;

    std::unique_ptr<NS::UI::ImGuiContext> m_imgui;
    std::unique_ptr<LevelEditorController> m_controller;

    NS::Editor::HierarchyPanel m_hierarchy; // 配置物ツリーのパネル。UI 状態を自分で持つ
    NS::Editor::InspectorPanel m_inspector; // 選択物の編集パネル。名前欄の状態を自分で持つ
    NS::Editor::ConsolePanel m_console;     // フレーム統計パネル
    NS::Editor::ToolModePanel m_toolMode;   // 編集モード切替パネル
    NS::Editor::AssetsPanel m_assets;       // アセットツリーと適用パネル
    NS::Editor::QuitModal m_quitModal;      // 終了時の保存確認モーダル
    NS::Editor::DockController m_dock;      // ドックホストとパネル全面化の状態
    NS::Editor::SceneViewPanel m_sceneView; // 編集 / 自由視点ビュー
    NS::Editor::GameViewPanel m_gameView;   // ゲーム視点ビュー

    bool m_uiVisible = true;
    bool m_lastModeWasPlay = false; // 前フレームがプレイモードだったか。タブ自動フォーカスの切替検知に使う
};