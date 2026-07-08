#pragma once

/// @file EditorLayer.h
/// @brief 編集モード UI Layer。 Debug / Development build 限定で active
///
/// @details Application::AddOverlay 経由で push される。 Regular Game Layer より後段で
/// OnUpdate / OnRender が走るため、 起動 scene の LevelPlayScene のプレイ進行を妨げずに
/// 編集機能を上乗せする。 cursor / palette / ギズモ / free-fly カメラ / モード切替といった編集状態は
/// `LevelEditorController` が保持し、 本 Layer はその生成・駆動と ImGui パネル描画を担う
///
/// GameDebug / GameRelease では CreateApplication が本 Layer を AddOverlay しないため、
/// 編集 UI が shipping ビルドに紛れ込まない。起動 scene は LevelPlayScene のまま



namespace NS::UI
{
    class ImGuiContext;
}

class LevelEditorController;

class EditorLayer : public NS::App::Layer
{
public:
    EditorLayer();
    ~EditorLayer() override;

    EditorLayer(const EditorLayer&) = delete;
    EditorLayer& operator=(const EditorLayer&) = delete;
    EditorLayer(EditorLayer&&) = delete;
    EditorLayer& operator=(EditorLayer&&) = delete;

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate() override;
    void OnRender() override;

private:
    static void HandleModeToggleInput(LevelEditorController& editor) noexcept;
    static void HandlePauseInput(LevelEditorController& editor) noexcept;
    /// プレイ中だけ F5 でエディタ UI 全体の表示/非表示をトグルする。 編集モードでは常に表示へ戻す
    void HandleUiVisibilityInput(LevelEditorController& editor) noexcept;
    static void RenderPauseModal(LevelEditorController& editor) noexcept;
    /// 終了要求を握って保存確認を出す。 確認済みなら true で終了を通し、 未確認なら modal を開いて false
    bool OnQuitRequested() noexcept;
    /// 終了確認 modal を描く。 保存して終了 / 保存せず終了 / キャンセルの 3 択
    void RenderQuitModal(LevelEditorController& editor) noexcept;
    /// 中央ノードを透過にした DockSpace を毎フレーム置き、 周囲パネルのドッキング先にする
    /// 中央は背景非描画 + 入力素通しなので、 全画面 3D とギズモがそのまま見え編集操作も届く
    static void RenderDockSpaceHost() noexcept;
    /// 右上に半透明の FPS / frame time オーバーレイを描画する。 追加の状態は持たない
    static void RenderFpsOverlay() noexcept;
    /// 解決済 RenderSettings の最終値と各フィールドの出所 default / scene / object を表示する
    /// 出所は scene / object override の has_value 突き合わせで逆算する。 Release では #if で除外
    static void RenderRenderSettingsPanel(LevelEditorController& editor) noexcept;
    /// 編集モード中にグリッド設置の Build ⇔ ギズモ変形の Object を切替える UI ボタンを描く
    static void RenderToolModePanel(LevelEditorController& editor) noexcept;
    /// 全配置物を一覧し、 行クリックで選択する。 grid/free バッジ付き、 選択中をハイライトする
    static void RenderHierarchyPanel(LevelEditorController& editor) noexcept;
    /// 選択中の対象を表示 / 編集する。 Player / Camera はその Component、 配置物は Transform + Component、
    /// area camera は専用 UI を出す。 free は Position/Scale 数値編集、 grid は昇格ボタン
    static void RenderInspectorPanel(LevelEditorController& editor) noexcept;
    /// Object モード中に Assets/ をフォルダツリーで出し、 ドロップ枠 / クリックで選択物体へ材質を適用する
    static void RenderMaterialsPanel(LevelEditorController& editor) noexcept;
    /// dir 直下を再帰描画する。 サブフォルダは TreeNode、 .mat はクリック適用 + ドラッグ可
    static void RenderAssetTree(const std::filesystem::path& dir, LevelEditorController& editor) noexcept;

    // ImGui ライフサイクルを Layer が所有する。Application は UI を知らないため editor が NewFrame/Render を駆動する
    // OnRender 内で BeginFrame → パネル構築 → EndFrame の順に回し、終端で UI キャプチャ状態を Input へ反映する
    std::unique_ptr<NS::UI::ImGuiContext> m_imgui;

    // 起動 scene の LevelPlayScene を編集するコントローラ。 OnAttach で scene へ束ねて Setup する
    std::unique_ptr<LevelEditorController> m_controller;

    // プレイ中に F5 でトグルするエディタ UI の表示フラグ。 編集モードでは毎フレーム true へ戻す
    bool m_uiVisible = true;

    // 終了確認の状態。 modalOpen 中は「保存しますか」 を出し、 confirmed で実際に終了させる
    bool m_quitModalOpen = false;
    bool m_quitConfirmed = false;
    bool m_quitSaveFailed = false;
};
