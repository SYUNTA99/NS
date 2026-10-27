#pragma once

#include "NSlib/App/Layer.h"
#include "NSlib/CoreObject/Scene/SceneManager.h"

#include <string>
#include <string_view>

namespace NS
{
    class Scene;
} // namespace NS

//! シーンの更新・描画をアプリから呼ぶゲームレイヤー
class Game : public NS::Layer
{
public:
    enum class StartScene
    {
        Load,  // 起動シーンを読む。読めなければ何も置かない空のシーンを立てる
        Empty, // 読まずに空のシーンを立てる。シーンの中身を後から入れ替える exe が選ぶ
    };

    explicit Game(std::string_view startScenePath = StartScenePath(), StartScene startScene = StartScene::Load);
    ~Game() override;

    //! 起動シーンの ContentRoot からの相対パス
    [[nodiscard]] static std::string_view StartScenePath() noexcept { return "Assets/Scenes/new_scene.scene"; }

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    Game(Game&&) = delete;
    Game& operator=(Game&&) = delete;

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate() override;
    void OnRender() override;

    //! ContentRoot からの相対パスでシーンを読み、立て直す
    //! 空・絶対パス・範囲外のパスや読み込み失敗は false で、現在のシーンを保つ
    bool LoadScene(std::string_view scenePath);

    //! 開始シーンを読み、成否を返す。結果は StartSceneLoaded に残る
    bool LoadStartScene();

    [[nodiscard]] bool StartSceneLoaded() const noexcept { return m_startSceneLoaded; }

    [[nodiscard]] NS::Scene* CurrentScene() noexcept;

    [[nodiscard]] static Game* Get() noexcept { return s_instance; }

private:
    //! 立てたシーンのコースを始める。startMusic が true ならシーンの設定の曲も鳴らし始める
    void StartLoadedScene(bool startMusic);

    std::string m_startScenePath;
    StartScene m_startScene = StartScene::Load;
    bool m_startSceneLoaded = false;

    NS::SceneManager m_scenes;

    static Game* s_instance;
};
