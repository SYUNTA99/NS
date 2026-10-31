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

    explicit Game(std::string_view startSceneName = StartSceneName(), StartScene startScene = StartScene::Load);
    ~Game() override;

    //! 起動シーンの名前。置き場所は NS::BuildLevelPath が決める
    [[nodiscard]] static std::string_view StartSceneName() noexcept { return "new_scene"; }

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    Game(Game&&) = delete;
    Game& operator=(Game&&) = delete;

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate() override;
    void OnRender() override;

    [[nodiscard]] NS::Scene* CurrentScene() noexcept;

    [[nodiscard]] static Game* Get() noexcept { return s_instance; }

private:
    std::string m_startSceneName;
    StartScene m_startScene = StartScene::Load;

    NS::SceneManager m_scenes;

    static Game* s_instance;
};
