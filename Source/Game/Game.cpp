#include "Game/Game.h"

#include "Game/Level/CourseDirector.h"
#include "NSlib/App/Application.h"
#include "NSlib/CoreObject/IGet/IGetSceneSystem.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/Core/Windows/Filesystem.h"

#include <optional>
#include <string>
#include <utility>

Game* Game::s_instance = nullptr;

Game::Game(std::string_view startScenePath, StartScene startScene)
    : NS::Layer("Game"), m_startScenePath(startScenePath), m_startScene(startScene)
{
    s_instance = this;
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
    // 遊べる物を代わりに合成すると、パッケージの取りこぼしが遊べる風の画面に隠れて気づけない
    if (m_startScene == StartScene::Load)
    {
        if (LoadStartScene())
        {
            return;
        }
        NS_LOG_ERROR(Game, "起動シーンを読めなかった。 空のシーンで立ち上げる");
    }
    (void)m_scenes.LoadScene(NS::MakeSceneJson());
    StartLoadedScene();
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

bool Game::LoadStartScene()
{
    m_startSceneLoaded = LoadScene(m_startScenePath);
    return m_startSceneLoaded;
}

bool Game::LoadScene(std::string_view scenePath)
{
    // 空のパスは ResolveUnder が ContentRoot 自体を返すので、フォルダを開けない失敗として出てしまう
    if (scenePath.empty())
    {
        NS_LOG_ERROR(Game, "LoadScene: シーンのパスが空");
        return false;
    }

    const std::optional<std::string> path =
        NS::OS::FileSystem::ResolveUnder(NS::OS::FileSystem::ContentRoot(), scenePath);
    if (!path)
    {
        NS_LOG_ERROR(Game, "LoadScene: シーンのパスが ContentRoot 配下に収まらない: {}", scenePath);
        return false;
    }

    nlohmann::json data;
    if (!NS::LoadSceneFromJsonFile(data, *path))
    {
        return false;
    }

    // 欠けた物の補完はしない。プレイヤーの居ないシーンはそのまま立て、足りない事実を隠さない
    (void)m_scenes.LoadScene(std::move(data));
    StartLoadedScene();
    return true;
}

void Game::StartLoadedScene()
{
    NS::Scene* scene = m_scenes.Current();
    if (scene == nullptr)
    {
        NS_LOG_ERROR(Game, "StartLoadedScene: シーンが立っていない");
        return;
    }
    // 自機の居ないシーンでも CourseDirector を作り、置いたアクターに始めの姿を控えさせる
    if (GL::Level::CourseDirector* director = NS::GetOrCreateSceneSystem<GL::Level::CourseDirector>(scene))
    {
        director->StartCourse();
    }
}

NS::Scene* Game::CurrentScene() noexcept
{
    return m_scenes.Current();
}
