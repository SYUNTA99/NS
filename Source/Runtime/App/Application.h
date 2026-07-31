#pragma once

#include "Runtime/App/Layers.h"
#include "Runtime/Core/Clock.h"
#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/Renderer.h"
#include "Runtime/Platform/Window.h"

#include <chrono>
#include <functional>
#include <memory>

namespace NS::App
{
    class Layer;
}

namespace NS::Platform
{
    class Input;
}

namespace NS::Object
{
    class AssetManager;
}

namespace NS::App
{

    /// 描画のプロジェクト既定値などは RendererDesc の settings 側で設定する
    struct ApplicationDesc
    {
        NS::Platform::WindowDesc window{};
        NS::Graphics::RendererDesc renderer{};
        float fixedDelta = NS::Core::FrameTimer::k_DefaultFixedDelta;
    };

    /// @brief 　サブシステムを管理しメインループを駆動するアプリケーション
    /// @details レイヤー構成は起動時に確定させる必要があり、メインループ実行中の動的な追加・削除は禁止。
    /// アプリケーションは単一インスタンスであり多重起動はできない。
    /// 時刻や補間割合が必要な場合は NS::Core::FrameTimer を直接参照する。
    class Application : public NS::Core::NonCopyable
    {
    public:
        explicit Application(const ApplicationDesc& desc);
        ~Application();

        /// 下層の Window / Renderer の構築に失敗した場合は false を返す
        [[nodiscard]] bool IsValid() const noexcept;

        /// 通常のレイヤーをオーバーレイより前へ追加する。メインループ開始前に呼ぶこと
        void AddLayer(std::unique_ptr<NS::App::Layer> layer);

        /// 描画が最後になるよう、オーバーレイを通常のレイヤーより後ろへ追加する。メインループ開始前に呼ぶこと
        void AddOverlay(std::unique_ptr<NS::App::Layer> overlay);

        /// @return 正常終了時に 0、初期化失敗またはレイヤーが空の場合は -1
        int Run();

        [[nodiscard]] NS::Platform::Window& Window() noexcept;
        [[nodiscard]] NS::Graphics::Renderer& Renderer() noexcept;
        [[nodiscard]] NS::Platform::Input& Input() noexcept;

        /// アプリの寿命に紐づくアセットキャッシュ
        [[nodiscard]] NS::Object::AssetManager& Assets() noexcept;

        /// 未構築時は nullptr を返す
        [[nodiscard]] static Application* Get() noexcept;

        /// 次のフレームでのループ終了要求。インスタンス未構築時は無視される
        static void Quit() noexcept;

        /// 終了してよいか判定するガードを登録する。false を返す間はループを終了させない
        /// @note 未登録の場合は終了要求がそのまま通る
        void SetQuitGuard(std::function<bool()> guard) noexcept;

    private:
        void Init();
        void MainLoop();
        void Shutdown();

        /// ウィンドウが閉じられた場合は即座に終了する。終了要求は guard に諮り、拒否されたら取り下げる
        [[nodiscard]] bool WantExit() noexcept;

        ApplicationDesc m_desc; ///< 構築時の設定の控え
        std::unique_ptr<NS::Platform::Window> m_window;
        std::unique_ptr<NS::Graphics::Renderer> m_renderer;
        // 下から順番に破棄されるから、アセットをRendererより先に解放させるためにここに書く
        std::unique_ptr<NS::Object::AssetManager> m_assets;
        Layers m_layers;
        bool m_valid = false;
        bool m_quitRequested = false;

        std::function<bool()> m_quitGuard;

        bool m_shutdownCalled = false; ///< 終了処理の二重呼び出しを防ぐフラグ
        std::chrono::steady_clock::time_point
            m_lastStutterWarnAt{}; ///< 連続して処理落ち警告を出さないための最終警告時刻

        static Application* s_instance;
    };

    /// ゲーム側で実装必須。設定やレイヤーを積み込んだ本体のインスタンスを生成して返す
    /// @note エントリポイントがゲームやエディタに依存しないように、本関数内でレイヤー構成を完結させること
    [[nodiscard]] std::unique_ptr<Application> CreateApplication();

} // namespace NS::App