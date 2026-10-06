#pragma once

#include "NSlib/App/Layers.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Graphics/Renderer.h"
#include "NSlib/Windows/Clock.h"
#include "NSlib/Windows/Window.h"

#include <chrono>
#include <functional>
#include <memory>

namespace NS
{
    class Layer;
}

namespace NS::OS
{
    class Input;
}

namespace NS::Obj
{
    class AssetManager;
}

namespace NS
{

    //! 描画のプロジェクト既定値などは RendererDesc の settings 側で設定する
    struct ApplicationDesc
    {
        NS::OS::WindowDesc window{};
        NS::Gfx::RendererDesc renderer{};
        float fixedDelta = NS::OS::FrameTimer::k_DefaultFixedDelta;
    };

    //! @brief サブシステムを管理しメインループを駆動するアプリケーション
    //! @details レイヤー構成は起動時に確定させる必要があり、メインループ実行中の動的な追加・削除は禁止
    //! アプリケーションは単一インスタンスであり多重起動はできない
    //! 時刻や補間割合が必要な場合は NS::OS::FrameTimer を直接参照する
    class Application : public NS::NonCopyable
    {
    public:
        explicit Application(const ApplicationDesc& desc);
        ~Application();

        //! 下層の Window / Renderer の構築に失敗した場合は false を返す
        [[nodiscard]] bool IsValid() const noexcept;

        //! 通常のレイヤーをオーバーレイより前へ追加する。メインループ開始前に呼ぶこと
        void AddLayer(std::unique_ptr<NS::Layer> layer);

        //! 描画が最後になるよう、オーバーレイを通常のレイヤーより後ろへ追加する。メインループ開始前に呼ぶこと
        void AddOverlay(std::unique_ptr<NS::Layer> overlay);

        //! @return 正常終了時に 0、初期化失敗またはレイヤーが空の場合は -1
        int Run();

        //! 起動時に作ったウィンドウ。Shutdown で破棄するので以降は参照できない
        [[nodiscard]] NS::OS::Window& Window() noexcept;
        [[nodiscard]] const NS::OS::Window& Window() const noexcept;
        //! 起動時に作ったレンダラー。破棄の時期はウィンドウと同じ
        [[nodiscard]] NS::Gfx::Renderer& Renderer() noexcept;
        [[nodiscard]] const NS::Gfx::Renderer& Renderer() const noexcept;
        //! プロセス全体で共有される入力。Application は所有しない
        [[nodiscard]] NS::OS::Input& Input() noexcept;
        [[nodiscard]] const NS::OS::Input& Input() const noexcept;

        //! アプリの寿命に紐づくアセットキャッシュ
        [[nodiscard]] NS::Obj::AssetManager& Assets() noexcept;
        [[nodiscard]] const NS::Obj::AssetManager& Assets() const noexcept;

        //! @brief カーソルの表示・固定とマウスの相対モードを必ず揃えて切り替える
        //! @param[in] captured 握る場合 true、出す場合 false
        //! @details 握ると、カーソルを隠し、固定し、相対モードにする。出すと 3 つとも戻す。
        //! 見えるカーソルと相対モードの併存は挙動が矛盾するので、3 つを別々に切り替える道は呼び手へ渡さない
        //! @note 固定の戻し先 Window::SetCursorLockPoint は握る・出すと別の話なので触らない
        void SetCursorCaptured(bool captured) noexcept;

        //! 未構築時は nullptr を返す
        [[nodiscard]] static Application* Get() noexcept;

        //! 次のフレームでのループ終了要求。インスタンス未構築時は無視される
        static void Quit() noexcept;

        //! 終了してよいか判定するガードを登録する。false を返す間はループを終了させない
        //! @note 未登録の場合は終了要求がそのまま通る
        void SetQuitGuard(std::function<bool()> guard) noexcept;

    private:
        void Init();
        void MainLoop();
        void Shutdown();

        //! ウィンドウが閉じられた場合は即座に終了する。終了要求は guard に諮り、拒否されたら取り下げる
        [[nodiscard]] bool WantExit() noexcept;

        std::unique_ptr<NS::OS::Window> m_window;
        std::unique_ptr<NS::Gfx::Renderer> m_renderer;
        // 下から順番に破棄されるから、アセットを Renderer より先に解放させるためにここに書く
        std::unique_ptr<NS::Obj::AssetManager> m_assets;
        Layers m_layers;
        bool m_valid = false;
        bool m_quitRequested = false;

        std::function<bool()> m_quitGuard;

        bool m_shutdownCalled = false; //!< 終了処理の二重呼び出しを防ぐフラグ
        std::chrono::steady_clock::time_point
            m_lastStutterWarnAt{}; //!< 連続して処理落ち警告を出さないための最終警告時刻

        static Application* s_instance;
    };

    //! ゲーム側で実装必須。設定やレイヤーを積み込んだ本体のインスタンスを生成して返す
    //! @note エントリポイントがゲームやエディタに依存しないように、本関数内でレイヤー構成を完結させること
    [[nodiscard]] std::unique_ptr<Application> CreateApplication();

} // namespace NS