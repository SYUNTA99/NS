#pragma once

/// @file Game.h
/// @brief Game — NS::App::Layer 継承の Game content layer。 SceneManager を所有し scene 群を駆動する
///
/// @details Application と Scene の間に位置する Layer
/// SceneManager を所有して scene 群を駆動する
/// 現時点では SceneManager へのパス・スルーのみだが、
/// クロス scene の共有状態 (設定 / セーブデータ) を持つ場所になる

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

    /// 現在 active な scene を `LevelEditorScene` として返す。 別 scene 型なら nullptr
    [[nodiscard]] LevelEditorScene* CurrentLevelEditorScene() noexcept;

    /// プロセス内の単一インスタンス取得。 Application は Layer を型不知で保持するため、
    /// Game 型を直接取得するには Game::Get() を使う
    [[nodiscard]] static Game* Get() noexcept { return s_instance; }

private:
    NS::Scene::SceneManager m_scenes;
    static Game* s_instance;
};
