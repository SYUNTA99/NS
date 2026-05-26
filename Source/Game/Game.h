#pragma once

/// @file Game.h
/// @brief Game — NS::App::Layer 継承の Game content layer。 SceneManager を所有し scene 群を駆動する。
///
/// @details Application (Engine) と Scene (content) の間に位置する Layer。
///  で Editor : Layer が追加された際は Application::PushOverlay(Editor) と組合せ、
/// SetActive(bool) で Edit/Play toggle を実装する。
///
/// ゲーム固有 cross-scene state (settings / save data / progress tracker) を将来持つ
/// 予定の場所だが、  完了時点では SceneManager への pass-through のみ。
///  retrospective で「Game に固有ロジック増えたか」 を確認し、 増えていなければ
/// 中間管理職として消す選択肢を再評価する。

#include "Framework/App/Layer.h"
#include "Framework/Scene/SceneManager.h"

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

private:
    NS::Scene::SceneManager m_scenes;
};
