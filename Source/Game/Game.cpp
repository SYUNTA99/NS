#include "Game/Game.h"

#include "Framework/App/Application.h"
#include "Game/Level/PlayState.h"
#include "Game/LevelPlayScene.h"

#if defined(NS_BUILD_DEBUG) || defined(NS_BUILD_DEV)
#include "Game/EditorLayer.h"
#endif

#include <memory>

namespace NS::App
{

    std::unique_ptr<Application> CreateApplication()
    {
        ApplicationDesc desc{};
        desc.window.title = "NS Game";
        desc.window.size = NS::Math::Size2D{1280, 720};
#ifdef NS_BUILD_DEBUG
        desc.renderer.enableDebugLayer = true;
#else
        desc.renderer.enableDebugLayer = false;
#endif
        auto app = std::make_unique<Application>(desc);

        // Layer / overlay の構成は Game 側で握る。 出荷 build には editor overlay を積まない
        // NS::App スコープ内では非修飾 Game が NS::Game 名前空間に解決されるため global の ::Game を明示する
        app->AddLayer(std::make_unique<::Game>());
#if defined(NS_BUILD_DEBUG) || defined(NS_BUILD_DEV)
        app->AddOverlay(std::make_unique<::EditorLayer>());
#endif
        return app;
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
