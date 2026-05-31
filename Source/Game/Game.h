#pragma once

/// @file Game.h
/// @brief Game — NS::App::Layer 継承の Game content layer。 SceneManager を所有し scene 群を駆動する。
///
/// @details Application (Engine) と Scene (content) の間に位置する Layer。
/// Editor : Layer が追加された際は Application::PushOverlay(Editor) と組合せ、
/// SetActive(bool) で Edit/Play toggle を実装する。
///
/// ゲーム固有 cross-scene state (settings / save data / progress tracker) を将来持つ
/// 予定の場所だが、 現時点では SceneManager への pass-through のみ。
/// 「Game に固有ロジック増えたか」 を確認し、 増えていなければ
/// 中間管理職として消す選択肢を再評価する。

#include "Framework/App/Layer.h"
#include "Framework/Scene/SceneManager.h"

class LevelEditorScene;

class Game : public NS::App::Layer
{
public:
    Game();
    ~Game() override;

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    Game(Game&&) = delete;
    Game& operator=(Game&&) = delete;

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate() override;
    void OnRender() override;

    /// 現在 active な scene を `LevelEditorScene` として返す。 別 scene 型なら nullptr。
    [[nodiscard]] LevelEditorScene* CurrentLevelEditorScene() noexcept;

    /// プロセス内 single instance accessor。 EditorLayer 等が type 確定で参照するために使用。
    /// Application::Get() ではなく Game::Get() を使う理由は、 Application が Layer を
    /// type 不知で持つため Game 派生型を直接取得するには cast が要るから。
    [[nodiscard]] static Game* Get() noexcept { return s_instance; }

private:
    NS::Scene::SceneManager m_scenes;
    static Game* s_instance;
};
