#pragma once

/// @file Game.h
/// @brief Game — NS::App::Layer 継承の Game content layer。 SceneManager を所有し scene 群を駆動する
///
/// @details Application と Scene の間に位置する Layer
/// 現時点では SceneManager へのパス・スルーのみだが、
/// クロス scene の共有状態として設定 / セーブデータを持つ場所になる

class LevelPlayScene;

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

    /// 現在 active な scene を play scene として返す。 自分で載せた LevelPlayScene が現役の間だけ
    /// 実体を返し、 未 load / 別 scene への差し替え後は nullptr
    [[nodiscard]] LevelPlayScene* CurrentPlayScene() noexcept;

    /// プロセス内の単一インスタンス取得。 Application は Layer を型不知で保持するため、
    /// Game 型を直接取得するには Game::Get() を使う
    [[nodiscard]] static Game* Get() noexcept { return s_instance; }

private:
    NS::Scene::SceneManager m_scenes;

    // 自分で載せた play scene の型付き控え。 所有は m_scenes 側で、 現役かどうかは識別で照合する
    LevelPlayScene* m_playScene = nullptr;

    static Game* s_instance;
};
