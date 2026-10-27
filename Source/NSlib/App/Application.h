#pragma once

#include "NSlib/App/Layers.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/Core/Windows/Window.h"

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

    /// 画面のクリア色などは D3DDeviceDesc 側で設定する
    struct ApplicationDesc
    {
        OS::WindowDesc window{};
        Gfx::D3DDeviceDesc device{};
        float fixedDelta = OS::FrameTimer::k_DefaultFixedDelta; // 秒
    };

    /// 単一インスタンスのメインループ。レイヤー構成は起動時に確定する
    class Application : public NonCopyable
    {
    public:
        explicit Application(const ApplicationDesc& desc);
        ~Application();

        [[nodiscard]] bool IsValid() const noexcept;

        /// 通常のレイヤーをオーバーレイより前へ追加する。メインループ開始前に呼ぶこと
        void AddLayer(std::unique_ptr<Layer> layer);

        /// オーバーレイを通常のレイヤーより後ろへ追加する。メインループ開始前に呼ぶこと
        void AddOverlay(std::unique_ptr<Layer> overlay);

        /// 正常終了時に 0、初期化失敗またはレイヤーが空の場合は -1 を返す
        int Run();

        /// Shutdown 後は参照不可
        [[nodiscard]] OS::Window& Window() noexcept;
        [[nodiscard]] const OS::Window& Window() const noexcept;
        /// Shutdown 後は参照不可
        [[nodiscard]] Gfx::D3DDevice& Device() noexcept;
        [[nodiscard]] const Gfx::D3DDevice& Device() const noexcept;
        [[nodiscard]] OS::Input& Input() noexcept;
        [[nodiscard]] const OS::Input& Input() const noexcept;

        /// Init 前および Shutdown 後は参照不可
        [[nodiscard]] AssetManager& Assets() noexcept;
        [[nodiscard]] const AssetManager& Assets() const noexcept;

        /// カーソルの表示・固定・相対モードをまとめて切り替える。固定点の設定は変更しない
        void SetCursorCaptured(bool captured) noexcept;

        /// 未構築時は nullptr を返す
        [[nodiscard]] static Application* Get() noexcept;

        /// 次のフレームでのループ終了を要求する。未構築時は無視される
        static void Quit() noexcept;

        /// 終了可否を判定するガードを登録する。false を返す間は終了要求を却下する
        void SetQuitGuard(std::function<bool()> guard) noexcept;

    private:
        void Init();
        void MainLoop();
        void Shutdown();
        [[nodiscard]] bool WantExit() noexcept;

        std::unique_ptr<OS::Window> m_window;
        std::unique_ptr<Gfx::D3DDevice> m_device;
        // D3DDevice より先にアセットを解放するため m_device の後に宣言する
        std::unique_ptr<AssetManager> m_assets;
        // m_assets より先に破棄してダングリング参照を防ぐ
        std::unique_ptr<AssetManagerScope> m_assetsScope;
        Layers m_layers;
        bool m_valid = false;
        bool m_quitRequested = false;

        std::function<bool()> m_quitGuard;

        bool m_shutdownCalled = false;
        bool m_attached = false;
        std::chrono::steady_clock::time_point m_lastStutterWarnAt{};

        static Application* s_instance;
    };

    /// ゲーム側で実装するエントリ関数。レイヤー構成済みのインスタンスを生成して返す
    [[nodiscard]] std::unique_ptr<Application> CreateApplication();

} // namespace NS
