#pragma once

/// @file EditorLayer.h
/// @brief 編集モード UI Layer。 Debug / Development build 限定で active。
///
/// @details Application::AddOverlay 経由で push される。 Regular Game Layer より後段で
/// OnUpdate / OnRender が走るため、 LevelEditorScene の進行を妨げず Editor 専用の入力
/// ハンドリング (Tab / Start で Edit↔Play flip、 P / Back で paused toggle) と Pause modal
/// 描画を担う。 Toolbar / palette / cursor preview 等の編集 UI は LevelEditorScene 内で
/// 描画済なので、 本 Layer は overlay 限定機能 (toggle + pause + 将来 HUD) に
/// 責務を絞る。
///
/// GameDebug / GameRelease では WinMain で本 Layer を AddOverlay しないため、 編集 UI が
/// shipping ビルドに紛れ込まない。

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
    /// 現在 active な LevelEditorScene を取得 (Game::Get() 経由)。 scene 未 load なら nullptr。
    [[nodiscard]] static LevelEditorScene* CurrentScene() noexcept;

    static void HandleModeToggleInput(LevelEditorScene& scene) noexcept;
    static void HandlePauseInput(LevelEditorScene& scene) noexcept;
    static void RenderPauseModal(LevelEditorScene& scene) noexcept;
    /// 右上に半透明の FPS / frame time オーバーレイを描画する。 ImGui の io.Framerate を
    /// 使うので追加の状態は持たない。 Debug / Dev build 限定 (EditorLayer 自体がそうなので継承)。
    static void RenderFpsOverlay() noexcept;
};
