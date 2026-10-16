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

namespace NS
{
    namespace OS
    {
        class Input;
    }
} // namespace NS

namespace NS
{
    class AssetManager;
    class AssetManagerScope;
}

namespace NS
{

    //! 描画のプロジェクト既定値などは RendererDesc の settings 側で設定する
    struct ApplicationDesc
    {
        OS::WindowDesc window{};
        Gfx::RendererDesc renderer{};
        float fixedDelta = OS::FrameTimer::k_DefaultFixedDelta;
    };

    //! 単一インスタンスのメインループ。レイヤーの構成は起動時に確定する
    class Application : public NonCopyable
    {
    public:
        explicit Application(const ApplicationDesc& desc);
        ~Application();

        //! 下層の Window / Renderer の構築に失敗した場合は false を返す
        [[nodiscard]] bool IsValid() const noexcept;

        //! 通常のレイヤーをオーバーレイより前へ追加する。メインループ開始前に呼ぶこと
        void AddLayer(std::unique_ptr<Layer> layer);

        //! 描画が最後になるよう、オーバーレイを通常のレイヤーより後ろへ追加する。メインループ開始前に呼ぶこと
        void AddOverlay(std::unique_ptr<Layer> overlay);

        //! @return 正常終了時に 0、初期化失敗またはレイヤーが空の場合は -1
        int Run();

        //! 起動時に作ったウィンドウ。Shutdown で破棄するので以降は参照できない
        [[nodiscard]] OS::Window& Window() noexcept;
        [[nodiscard]] const OS::Window& Window() const noexcept;
        //! 起動時に作ったレンダラー。破棄の時期はウィンドウと同じ
        [[nodiscard]] Gfx::Renderer& Renderer() noexcept;
        [[nodiscard]] const Gfx::Renderer& Renderer() const noexcept;
        //! プロセス全体で共有される入力。Application は所有しない
        [[nodiscard]] OS::Input& Input() noexcept;
        [[nodiscard]] const OS::Input& Input() const noexcept;

        //! アプリの寿命に紐づくアセットキャッシュ
        [[nodiscard]] AssetManager& Assets() noexcept;
        [[nodiscard]] const AssetManager& Assets() const noexcept;

        //! 表示・固定・相対モードをまとめて切り替える。captured が true ならカーソルを隠す
        //! 固定点の設定は変更しない
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

        std::unique_ptr<OS::Window> m_window;
        std::unique_ptr<Gfx::Renderer> m_renderer;
        // 下から順番に破棄されるから、アセットを Renderer より先に解放させるためにここに書く
        std::unique_ptr<AssetManager> m_assets;
        // AssetManager::Get() が m_assets を返す間。m_assets より先に壊して、捨てた物を返さない
        std::unique_ptr<AssetManagerScope> m_assetsScope;
        Layers m_layers;
        bool m_valid = false;
        bool m_quitRequested = false;

        std::function<bool()> m_quitGuard;

        bool m_shutdownCalled = false; //!< 終了処理の二重呼び出しを防ぐフラグ
        bool m_attached = false;       //!< Init で全レイヤーの OnAttach を呼んだか
        std::chrono::steady_clock::time_point
            m_lastStutterWarnAt{}; //!< 連続して処理落ち警告を出さないための最終警告時刻

        static Application* s_instance;
    };

    //! ゲーム側で実装必須。設定やレイヤーを積み込んだ本体のインスタンスを生成して返す
    //! @note エントリポイントがゲームやエディタに依存しないように、本関数内でレイヤー構成を完結させること
    [[nodiscard]] std::unique_ptr<Application> CreateApplication();

} // namespace NS
