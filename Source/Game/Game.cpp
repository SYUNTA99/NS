#include "Game/Game.h"

#include "Runtime/App/Application.h"
#include "Runtime/Core/Filesystem.h"
#include "Runtime/Object/Components/ThirdPersonFollowComponent.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Scene/SceneJson.h"
#include "Runtime/Platform/Input.h"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace
{
    /// @brief シーン名からファイルパスを組む
    /// @details 名前は Scenes フォルダ直下の 1 枚を指す。区切りや ".." が混ざる名前は外へ抜けるので弾く
    std::optional<std::filesystem::path> BuildScenePath(std::string_view sceneName)
    {
        if (sceneName.empty())
            return std::nullopt;
        if (sceneName.find("..") != std::string_view::npos)
            return std::nullopt;
        if (sceneName.find('/') != std::string_view::npos)
            return std::nullopt;
        if (sceneName.find('\\') != std::string_view::npos)
            return std::nullopt;

        std::filesystem::path path = NS::Core::FileSystem::ContentRoot() / "Assets" / "Scenes";
        path /= std::string{sceneName} + ".scene";
        return path;
    }
} // namespace

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
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
    {
        NS_LOG_ERROR(Game, "Game::OnAttach: Application::Get()==null");
        return;
    }

    // AssetManager とレンダラーを預ける。 立てるシーンがここから受け取る
    // ※編集機能は Editor がオーバーレイとして乗せる想定
    m_scenes.SetAssets(&app->Assets());
    m_scenes.SetRenderer(&app->Renderer());

    // 同梱シーンを読む。 読めなければ何も置かない空のシーンを立てる
    // 遊べる物を代わりに合成すると、 パッケージの取りこぼしが遊べる風の画面に隠れて気づけない
    if (!LoadScene("new_scene"))
    {
        NS_LOG_ERROR(Game, "起動シーンを読めなかった。 空のシーンで立ち上げる");
        (void)m_scenes.LoadScene(NS::Object::SceneData{});
    }

    NS::Object::Scene* scene = m_scenes.Current();
    if (scene == nullptr)
    {
        NS_LOG_ERROR(Game, "Game::OnAttach: シーンが立たなかった");
        return;
    }

    // 走行のやり直しが読む凍結スナップショットをここで捕まえる。 世界はシーンが読み込みから回している
    (void)scene->BeginPlayBaseline();

    // 追従カメラは生成直後は休止している。 出荷はプレイしかないので起動で有効化する
    scene->World().ForEachComponent<NS::Object::ThirdPersonFollowComponent>(
        [](NS::Object::ThirdPersonFollowComponent& follow) { follow.SetActive(true); });

    // カーソルを消し、 マウスを相対モードにして視点操作をカーソル位置から切り離す
    // Esc で出すまで非表示のまま。 出し直しは OnUpdate の Esc 処理が行う
    app->Window().SetCursorVisible(false);
    app->Input().Mouse().SetRelativeMode(true);
}

void Game::OnDetach()
{
    m_scenes.UnloadScene();
}

void Game::OnUpdate()
{
    // プレイ中の Esc は 2 段階。 1 回目で隠したカーソルを出し、 出ている状態の 2 回目で終了する
    // カーソルの状態がそのまま段階の記録になる。 世界が止まっている編集モードの Esc はエディタが処理する
    const NS::Object::Scene* scene = m_scenes.Current();
    if (scene != nullptr && scene->IsSimulationEnabled())
    {
        if (auto* app = NS::App::Application::Get())
        {
            if (app->Input().Keyboard().IsPressed(NS::Platform::Key::Escape))
            {
                if (!app->Window().IsCursorVisible())
                {
                    // カーソルを出すなら相対モードも解く。 見えるカーソルと相対モードの併存は挙動が矛盾する
                    app->Window().SetCursorVisible(true);
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

    // 世界の駆動はシーン自身が持つ。 ここはシーン更新を呼ぶだけ
    m_scenes.Update();
}

void Game::OnRender()
{
    m_scenes.Render();
    // scene が最後に bind した描画先へ UI を重ねる。単体起動は backbuffer、editor はビュー列の
    // 末尾に居る Game ビューがそのまま残っているので、どちらもゲームの絵の上に載る
    if (auto* app = NS::App::Application::Get())
        m_ui.Render(app->Renderer());
}

bool Game::LoadScene(std::string_view sceneName)
{
    const std::optional<std::filesystem::path> path = BuildScenePath(sceneName);
    if (!path)
    {
        NS_LOG_ERROR(Game, "LoadScene: シーン名が不正: {}", sceneName);
        return false;
    }

    NS::Object::SceneData data;
    if (!NS::Object::LoadSceneFromJsonFile(data, *path))
    {
        return false;
    }

    // 欠けた物の補完はしない。 プレイヤーの居ないシーンはそのまま立て、 足りない事実を隠さない
    (void)m_scenes.LoadScene(std::move(data));
    return true;
}

NS::Object::Scene* Game::CurrentScene() noexcept
{
    return m_scenes.Current();
}
