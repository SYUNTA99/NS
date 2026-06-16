#include "Game/Game.h"

#include "Framework/App/Application.h"
#include "Game/Level/PlayState.h"
#include "Game/LevelPlayScene.h"

#include <memory>

Game* Game::s_instance = nullptr;

Game::Game() : NS::App::Layer("Game")
{
    s_instance = this;
}

Game::~Game()
{
    if (s_instance == this)
        s_instance = nullptr;
}

void Game::OnAttach()
{
    // scene は出荷 / 開発とも LevelPlayScene の 1 種類だけ。 編集機能は EditorLayer (overlay) が乗せる
    m_scenes.LoadScene(std::make_unique<LevelPlayScene>());
}

void Game::OnDetach()
{
    m_scenes.LoadScene(nullptr);
}

void Game::OnUpdate()
{
    m_scenes.Update();

    // ハザード接触死 (playerHealth==0) で即 Quit。落下死は scene 側で
    // respawn に乗るのでここでは観測しない (deathTriggered は落下死でも立つため区別できない)
    if (auto* scene = CurrentPlayScene())
    {
        if (scene->Play().playerHealth <= 0)
            NS::App::Application::Quit();
    }
}

void Game::OnRender()
{
    m_scenes.Render();
}

LevelPlayScene* Game::CurrentPlayScene() noexcept
{
    // boot scene は LevelPlayScene の 1 種類だけなので静的 cast で足りる
    return static_cast<LevelPlayScene*>(m_scenes.Current());
}
