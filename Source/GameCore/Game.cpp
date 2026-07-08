#include "GameCore/Game.h"

#include "GameCore/LevelPlayScene.h"
#include "GameCore/Theme/ThemeRegistry.h"

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
    // テーマは scene 構築前に読む。 読めない分は組み込み既定値のままなので起動は止まらない
    NS::GameCore::Theme::LoadThemesFromDirectory(NS::Core::FileSystem::ContentRoot() / "Assets" / "Themes");

    // scene は出荷 / 開発とも LevelPlayScene の 1 種類だけ。 編集機能は overlay の EditorLayer が乗せる
    m_scenes.LoadScene(std::make_unique<LevelPlayScene>());
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

LevelPlayScene* Game::CurrentPlayScene() noexcept
{
    // 別の SceneBase 派生が load されても未定義動作にせず nullptr へ倒すため検査付きの動的キャストを使う
    return dynamic_cast<LevelPlayScene*>(m_scenes.Current());
}
