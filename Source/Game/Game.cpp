#include "Game/Game.h"

#include "Game/Level/GameScene.h"
#include "NSlib/App/Application.h"
#include "NSlib/Core/Windows/FileSystem.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Gfx/D3DShader.h"

Game* Game::s_instance = nullptr;

Game::Game(std::string_view startSceneName, StartScene startScene)
    : NS::Layer("Game"), m_startSceneName(startSceneName), m_startScene(startScene)
{
    s_instance = this;
    // 物ごとの見た目の口のファイル (SurfaceHooks.hlsli) を Game の物から先に読ませる。シーンの素材を読む前に渡す
    NS::Gfx::SetShaderIncludeDirectories(
        {NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Shaders/Game")});
}

Game::~Game()
{
    if (s_instance == this)
    {
        s_instance = nullptr;
    }
}

void Game::OnAttach()
{
    NS::Application* app = NS::Application::Get();
    if (app == nullptr)
    {
        NS_LOG_ERROR(Game, "Game::OnAttach: Application::Get()==null");
        return;
    }

    // LoadScene で立てるシーンへ引き継ぐので、LoadScene より前に呼ぶ
    m_scenes.SetDevice(&app->Device());
    m_scenes.SetAudioDevice(app->AudioDevice());

    // 同梱シーンを読む。読めなければ何も置かない空のシーンを立てる
    // 遊べる物を代わりに合成すると、パッケージの取りこぼしが遊べる風の画面に隠れて気づけない。
    // 欠けた物の補完もしない。プレイヤーの居ないシーンはそのまま立て、足りない事実を隠さない
    if (m_startScene == StartScene::Load)
    {
        if (m_scenes.LoadScene<GL::Level::GameScene>(m_startSceneName) != nullptr)
        {
            return;
        }
        NS_LOG_ERROR(Game, "起動シーンを読めなかった。 空のシーンで立ち上げる");
    }
    (void)m_scenes.LoadEmptyScene<GL::Level::GameScene>();
}

void Game::OnDetach()
{
    m_scenes.UnloadScene();
}

void Game::OnUpdate()
{
    m_scenes.Update();
}

void Game::OnRender()
{
    m_scenes.Render();
}

NS::Scene* Game::CurrentScene() noexcept { return m_scenes.Current(); }
