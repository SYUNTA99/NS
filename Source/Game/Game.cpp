#include "Game/Game.h"

#include "Game/Level/CourseDirector.h"
#include "NSlib/App/Application.h"
#include "NSlib/Object/IUse/IUseSceneObj.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Scene/SceneJson.h"
#include "NSlib/Windows/Filesystem.h"

#include <optional>
#include <string>
#include <utility>

namespace
{
    std::optional<std::string> ResolveScenePath(std::string_view scenePath)
    {
        return NS::OS::FileSystem::ResolveUnder(NS::OS::FileSystem::ContentRoot(), scenePath);
    }
} // namespace

Game* Game::s_instance = nullptr;

Game::Game(std::string_view startScenePath) : NS::Layer("Game"), m_startScenePath(startScenePath)
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

    // AssetManager とレンダラーを預ける。立てるシーンがここから受け取る
    // 編集機能は Editor がオーバーレイとして乗せる
    m_scenes.SetAssets(&app->Assets());
    m_scenes.SetRenderer(&app->Renderer());

    // 同梱シーンを読む。読めなければ何も置かない空のシーンを立てる
    // 遊べる物を代わりに合成すると、パッケージの取りこぼしが遊べる風の画面に隠れて気づけない
    if (!LoadStartScene())
    {
        NS_LOG_ERROR(Game, "起動シーンを読めなかった。 空のシーンで立ち上げる");
        (void)m_scenes.LoadScene(NS::Obj::MakeSceneJson());
        StartLoadedScene();
    }
}

void Game::OnDetach()
{
    m_scenes.UnloadScene();
}

void Game::OnUpdate()
{
    // 世界の駆動はシーン自身が持つ。プレイ中のカーソルと Esc は構成ごとの外枠 (StandaloneLayer / Editor) が持つ
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

    const std::optional<std::string> path = ResolveScenePath(scenePath);
    if (!path)
    {
        NS_LOG_ERROR(Game, "LoadScene: シーンのパスが ContentRoot 配下に収まらない: {}", scenePath);
        return false;
    }

    nlohmann::json data;
    if (!NS::Obj::LoadSceneFromJsonFile(data, *path))
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
    NS::Obj::Scene* scene = m_scenes.Current();
    if (scene == nullptr)
    {
        NS_LOG_ERROR(Game, "StartLoadedScene: シーンが立っていない");
        return;
    }
    // 自機の居ないシーンでも CourseDirector を作って凍結を取る。読み直しの後に前のシーンの凍結が残らない
    if (NS::Game::Level::CourseDirector* director =
            NS::Obj::GetOrCreateSceneObj<NS::Game::Level::CourseDirector>(*scene))
    {
        director->StartCourse();
    }
}

NS::Obj::Scene* Game::CurrentScene() noexcept
{
    return m_scenes.Current();
}
