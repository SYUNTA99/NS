#include "Game/Game.h"

#include "Runtime/App/Application.h"
#include "Runtime/Object/Components/ThirdPersonFollow.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Scene/SceneJson.h"
#include "Runtime/Platform/Filesystem.h"
#include "Runtime/Platform/Input.h"

#include <optional>
#include <string>
#include <utility>

namespace
{
    std::optional<std::string> ResolveScenePath(std::string_view scenePath)
    {
        return NS::Platform::FileSystem::ResolveUnder(NS::Platform::FileSystem::ContentRoot(), scenePath);
    }
} // namespace

Game* Game::s_instance = nullptr;

Game::Game(std::string_view startScenePath) : NS::App::Layer("Game"), m_startScenePath(startScenePath)
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
    NS::App::Application* app = NS::App::Application::Get();
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
        (void)m_scenes.LoadScene(NS::Obj::SceneData{});
    }

    NS::Obj::Scene* scene = m_scenes.Current();
    if (scene == nullptr)
    {
        NS_LOG_ERROR(Game, "Game::OnAttach: シーンが立たなかった");
        return;
    }

    // 走行のやり直しが読む凍結スナップショットをここで捕まえる。世界はシーンが読み込みから回している
    (void)scene->BeginPlayBaseline();

    // 追従カメラは生成直後は休止している。出荷はプレイしかないので起動で有効化する
    scene->Objects().ForEachComponent<NS::Obj::ThirdPersonFollow>(
        [](NS::Obj::ThirdPersonFollow& follow) { follow.SetActive(true); });

    // カーソルを消し、マウスを相対モードにして視点操作をカーソル位置から切り離す
    // Esc で出すまで非表示のまま。出し直しは OnUpdate の Esc 処理が行う
    app->Window().SetCursorVisible(false);
    app->Window().SetCursorLocked(true);
    app->Input().Mouse().SetRelativeMode(true);
}

void Game::OnDetach()
{
    m_scenes.UnloadScene();
}

void Game::OnUpdate()
{
    // プレイ中の Esc は 2 段階。1 回目で隠したカーソルを出し、出ている状態の 2 回目で終了する
    // カーソルの状態がそのまま段階の記録になる。世界が止まっている編集モードの Esc はエディタが処理する
    const NS::Obj::Scene* scene = m_scenes.Current();
    if (scene != nullptr && scene->IsSimulationEnabled())
    {
        if (NS::App::Application* app = NS::App::Application::Get())
        {
            if (app->Input().Keyboard().IsPressed(NS::Platform::Key::Escape))
            {
                if (!app->Window().IsCursorVisible())
                {
                    // カーソルを出すなら相対モードも解く。見えるカーソルと相対モードの併存は挙動が矛盾する
                    app->Window().SetCursorVisible(true);
                    app->Window().SetCursorLocked(false);
                    app->Input().Mouse().SetRelativeMode(false);
                }
                else
                {
                    NS::App::Application::Quit();
                }
                return;
            }
        }
    }

    // 世界の駆動はシーン自身が持つ。ここはシーン更新を呼ぶだけ
    m_scenes.Update();
}

void Game::OnRender()
{
    m_scenes.Render();
    // scene が最後に bind した描画先へ UI を重ねる。単体起動はバックバッファ、editor はビュー列の
    // 末尾にある Game ビューがそのまま残るので、どちらもゲームの絵の上に載る
    if (NS::App::Application* app = NS::App::Application::Get())
        m_ui.Render(app->Renderer());
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

    NS::Obj::SceneData data;
    if (!NS::Obj::LoadSceneFromJsonFile(data, *path))
    {
        return false;
    }

    // 欠けた物の補完はしない。プレイヤーの居ないシーンはそのまま立て、足りない事実を隠さない
    (void)m_scenes.LoadScene(std::move(data));
    return true;
}

NS::Obj::Scene* Game::CurrentScene() noexcept
{
    return m_scenes.Current();
}
