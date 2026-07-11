#include "Game/Game.h"

#include "Game/LevelPlayScene.h"
#include "Game/Theme/ThemeRegistry.h"

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
    NS::Game::Theme::LoadThemesFromDirectory(NS::Core::FileSystem::ContentRoot() / "Assets" / "Themes");

    // scene は出荷 / 開発とも LevelPlayScene の 1 種類だけ。 編集機能は overlay の EditorLayer が乗せる
    auto playScene = std::make_unique<LevelPlayScene>();
    m_playScene = playScene.get();
    m_scenes.LoadScene(std::move(playScene));
}

void Game::OnDetach()
{
    m_playScene = nullptr;
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
    // 自分で載せた 1 体が現役かを識別で照合する。 別 scene に差し替わっていれば nullptr へ倒れ、
    // 実行時型情報に頼らず型付きの控えを安全に返せる
    if (m_scenes.Current() == m_playScene)
        return m_playScene;
    return nullptr;
}
