#include "Game/Game.h"

#include "Framework/App/Application.h"
#include "Game/LevelEditorScene.h"

#include <memory>

namespace NS::App
{

    std::unique_ptr<Application> CreateApplication()
    {
        ApplicationDesc desc{};
        desc.window.title = "NS Game";
        desc.window.size = NS::Core::Size2D{1280, 720};
#ifdef NS_BUILD_DEBUG
        desc.renderer.enableDebugLayer = true;
#else
        desc.renderer.enableDebugLayer = false;
#endif
        return std::make_unique<Application>(desc);
    }

} // namespace NS::App

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
    m_scenes.LoadScene(std::make_unique<LevelEditorScene>());
}

void Game::OnDetach()
{
    m_scenes.LoadScene(nullptr);
}

void Game::OnUpdate()
{
    m_scenes.Update();
}

void Game::OnRender()
{
    m_scenes.Render();
}

LevelEditorScene* Game::CurrentLevelEditorScene() noexcept
{
    // 現状 NSGame の active scene は LevelEditorScene 一択。 他種 scene を後段で導入したら
    // dynamic_cast 化を検討するが、 今は static_cast で十分。
    return static_cast<LevelEditorScene*>(m_scenes.Current());
}
