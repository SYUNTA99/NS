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

Game::Game() : NS::App::Layer("Game") {}

Game::~Game() = default;

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
