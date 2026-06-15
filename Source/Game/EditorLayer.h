#pragma once

/// @file EditorLayer.h
/// @brief 編集モード UI Layer。 Debug / Development build 限定で active
///
/// @details Application::AddOverlay 経由で push される。 Regular Game Layer より後段で
/// OnUpdate / OnRender が走るため、 LevelEditorScene の進行を妨げず Editor 専用の入力
/// ハンドリング (Tab / Start で Edit↔Play flip、 P / Back で paused toggle) と Pause modal
/// 描画を担う。 Toolbar / palette / cursor preview 等の編集 UI は LevelEditorScene 内で
/// 描画済なので、 本 Layer は overlay 限定機能 (toggle + pause + 将来 HUD) に
/// 責務を絞る
///
/// GameDebug / GameRelease では WinMain で本 Layer を AddOverlay しないため、 編集 UI が
/// shipping ビルドに紛れ込まない

#include "Framework/App/Layer.h"

#include <filesystem>

class LevelEditorScene;

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
    /// 現在 active な LevelEditorScene を取得 (Game::Get() 経由)。 scene 未 load なら nullptr
    [[nodiscard]] static LevelEditorScene* CurrentScene() noexcept;

    static void HandleModeToggleInput(LevelEditorScene& scene) noexcept;
    static void HandlePauseInput(LevelEditorScene& scene) noexcept;
    static void RenderPauseModal(LevelEditorScene& scene) noexcept;
    /// 中央ノードを透過にした DockSpace を毎フレーム置き、 周囲パネルのドッキング先にする
    /// 中央は背景非描画 + 入力素通しなので、 全画面 3D とギズモがそのまま見え編集操作も届く
    static void RenderDockSpaceHost() noexcept;
    /// 右上に半透明の FPS / frame time オーバーレイを描画する。 追加の状態は持たない
    static void RenderFpsOverlay() noexcept;
    /// 解決済 RenderSettings の最終値と各フィールドの出所 (default / scene / object) を表示する
    /// 出所は scene / object override の has_value 突き合わせで逆算する。 Release では #if で除外
    static void RenderRenderSettingsPanel(LevelEditorScene& scene) noexcept;
    /// 編集モード中に Build (グリッド設置) ⇔ Object (ギズモ変形) を切替える UI ボタンを描く
    static void RenderToolModePanel(LevelEditorScene& scene) noexcept;
    /// 全配置物を一覧し、 行クリックで選択する。 grid/free バッジ付き、 選択中をハイライトする
    static void RenderHierarchyPanel(LevelEditorScene& scene) noexcept;
    /// 選択中の配置物のプロパティを表示 / 編集する。 free は Position/Scale 数値編集、 grid は昇格ボタン
    static void RenderInspectorPanel(LevelEditorScene& scene) noexcept;
    /// Object モード中に Assets/ をフォルダツリーで出し、 ドロップ枠 / クリックで選択物体へ材質を適用する
    static void RenderMaterialsPanel(LevelEditorScene& scene) noexcept;
    /// dir 直下を再帰描画する。 サブフォルダは TreeNode、 .mat はクリック適用 + ドラッグ可
    static void RenderAssetTree(const std::filesystem::path& dir, LevelEditorScene& scene) noexcept;
};
