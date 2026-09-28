#pragma once

#include "Runtime/App/Layer.h"
#include "Runtime/Object/Scene/SceneManager.h"
#include "Runtime/UI/UISystem.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace NS::Obj
{
    class Scene;
} // namespace NS::Obj

//! @brief アプリケーション層とシーン管理層を仲介するメインゲームレイヤー
class Game : public NS::App::Layer
{
public:
    //! @brief 開始シーンのパスを控える。読み込みは OnAttach
    //! @param[in] startScenePath ContentRoot からの相対パス。既定は同梱の開始シーン
    explicit Game(std::string_view startScenePath = "Assets/Scenes/new_scene.scene");
    ~Game() override;

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    Game(Game&&) = delete;
    Game& operator=(Game&&) = delete;

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate() override;
    void OnRender() override;

    //! @brief ContentRoot 配下のシーンを読み込み、立て直す
    //! @details 絶対パスと ContentRoot の外へ出るパスは受け付けない
    //! @param[in] scenePath ContentRoot からの相対パス。空文字列は受け付けない
    //! @return 読めなければ false を返し、その時は今のシーンをそのまま保つ
    bool LoadScene(std::string_view scenePath);

    //! @brief 構築時に受けた開始シーンを読む
    //! @details 結果は StartSceneLoaded() に残る
    //! @return 読めた場合 true、それ以外の場合は false
    bool LoadStartScene();

    //! 直近の LoadStartScene が読めた場合 true。呼ぶ前は false
    [[nodiscard]] bool StartSceneLoaded() const noexcept { return m_startSceneLoaded; }

    //! @brief 現在ロードされているシーンを取得する
    //! @return シーン未ロードなら nullptr を返す
    [[nodiscard]] NS::Obj::Scene* CurrentScene() noexcept;

    //! @brief ゲーム画面へ重ねる UI。HUD やメニューはこの Root() の下に組む
    [[nodiscard]] NS::UI::UISystem& Ui() noexcept { return m_ui; }

    //! @brief シングルトンインスタンスを取得する
    [[nodiscard]] static Game* Get() noexcept { return s_instance; }

    //! @brief プレイ中に Esc を押した時の応答
    enum class EscapeResponse : std::uint8_t
    {
        ReleaseCursor, // カーソルを出し、固定と相対を解く
        Quit,          // アプリを終える
        None           // 何もしない
    };

    //! @brief Esc の応答を決める材料
    struct EscapeQuery
    {
        bool cursorVisible = false;    // 押した時にカーソルが出ていたか
        bool secondEscapeQuits = true; // カーソルが出ている状態の Esc でアプリを終えるか
    };

    //! プレイ中の Esc の応答を返す。1 回目でカーソルを出し、出ている状態の 2 回目は許されていれば終える
    [[nodiscard]] static EscapeResponse ResolveEscape(EscapeQuery query) noexcept;

    //! @brief カーソルが出ている状態の Esc でアプリを終えるかを設定する
    //! @details 既定は true。エディタはプレイを Esc で落とさないよう false にし、プレイの終わりは自前の操作に任せる
    //! @param[in] quits 終える場合 true
    void SetSecondEscapeQuits(bool quits) noexcept { m_secondEscapeQuits = quits; }

    //! カーソルが出ている状態の Esc でアプリを終える場合 true、それ以外の場合は false
    [[nodiscard]] bool SecondEscapeQuits() const noexcept { return m_secondEscapeQuits; }

private:
    std::string m_startScenePath;
    bool m_startSceneLoaded = false;
    bool m_secondEscapeQuits = true; // カーソルが出ている状態の Esc でアプリを終えるか

    NS::Obj::SceneManager m_scenes; // シーンの所有と遷移管理
    NS::UI::UISystem m_ui;          // ゲーム画面の UI。world 描画の後に重ねる

    static Game* s_instance;
};
