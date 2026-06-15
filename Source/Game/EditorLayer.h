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
    /// 右上に半透明の FPS / frame time オーバーレイを描画する。 追加の状態は持たない
    static void RenderFpsOverlay() noexcept;
    /// 解決済 RenderSettings の最終値と各フィールドの出所 (default / scene / object) を表示する
    /// 出所は scene / object override の has_value 突き合わせで逆算する。 Release では #if で除外
    static void RenderRenderSettingsPanel(LevelEditorScene& scene) noexcept;
    /// 編集モード中に Build (グリッド設置) ⇔ Object (ギズモ変形) を切替える UI ボタンを描く
    static void RenderToolModePanel(LevelEditorScene& scene) noexcept;
};
