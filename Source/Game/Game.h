#pragma once

#include "Runtime/App/Layer.h"
#include "Runtime/Object/Scene/SceneManager.h"
#include "Runtime/UI/UISystem.h"

#include <string_view>

namespace NS::Object
{
    class Scene;
} // namespace NS::Object

//! @brief アプリケーション層とシーン管理層を仲介するメインゲームレイヤー
class Game : public NS::App::Layer
{
public:
    Game();
    ~Game() override;

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    Game(Game&&) = delete;
    Game& operator=(Game&&) = delete;

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate() override;
    void OnRender() override;

    //! @brief シーン名を指定してシーンを読み込み、立て直す
    //! @details 名前は Scenes/<name>.scene に対応する。パス区切りや ".." を含む名前は受け付けない
    //! @return 読めなければ false を返し、その時は今のシーンをそのまま保つ
    bool LoadScene(std::string_view sceneName);

    //! @brief 現在ロードされているシーンを取得する
    //! @return シーン未ロードなら nullptr を返す
    [[nodiscard]] NS::Object::Scene* CurrentScene() noexcept;

    //! @brief ゲーム画面へ重ねる UI。HUD やメニューはこの Root() の下に組む
    [[nodiscard]] NS::UI::UISystem& Ui() noexcept { return m_ui; }

    //! @brief シングルトンインスタンスを取得する
    [[nodiscard]] static Game* Get() noexcept { return s_instance; }

private:
    NS::Object::SceneManager m_scenes; // シーンの所有と遷移管理
    NS::UI::UISystem m_ui;              // ゲーム画面の UI。world 描画の後に重ねる

    static Game* s_instance;
};
