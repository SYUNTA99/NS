#pragma once

#include "ns/graphics/renderer.h"
#include "ns/platform/window.h"

#include <memory>

namespace ns::platform
{
    class Input;
}

namespace ns::app
{

    class Scene;

    /// Application 構築パラメータ。
    /// Window / Renderer の Desc を内包し、メインループの固定 delta + クリアカラーも持つ。
    struct ApplicationDesc
    {
        ns::platform::WindowDesc window{};
        ns::graphics::RendererDesc renderer{};
        /// 固定 Update の delta 秒。デフォルト 1/60。
        float fixedDelta = 1.0f / 60.0f;
        /// BeginFrame のクリアカラー (RGBA)。Application が毎フレーム適用する。
        float clearR = 0.10f;
        float clearG = 0.10f;
        float clearB = 0.15f;
        float clearA = 1.0f;
    };

    /// Game のメインループ責務を担う Application (/127/130)。
    /// インスタンス: Window / Renderer / Input / FrameTimer を RAII 所有。
    /// Static: Get / Quit / DeltaTime / Time / Alpha でグローバルアクセサ提供。
    /// Game 側で継承する必要は無い (final 寄り設計)。Game は Scene 派生で書く。
    /// 多重起動禁止 (s_instance 単一保持で assert)。
    class Application
    {
    public:
        struct Impl;

        explicit Application(const ApplicationDesc& desc);
        ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;
        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;

        /// 構築成功判定。Window / Renderer のいずれかが失敗していたら false。
        [[nodiscard]] bool IsValid() const noexcept;

        /// Init → MainLoop → Shutdown を順に呼ぶ Template Method。
        /// initialScene が nullptr または IsValid()==false なら -1 を返して即終了。
        int Run(std::unique_ptr<Scene> initialScene);

        [[nodiscard]] ns::platform::Window& Window() noexcept;
        [[nodiscard]] ns::graphics::Renderer& Renderer() noexcept;
        [[nodiscard]] ns::platform::Input& Input() noexcept;

        /// 現在の Application インスタンス (/)。未構築時は nullptr。
        [[nodiscard]] static Application* Get() noexcept;
        /// 次フレームの MainLoop ループ抜け要求。Get() が nullptr の場合は no-op。
        static void Quit() noexcept;
        /// 直近 frame の variable delta (秒)。未構築時は 0。
        [[nodiscard]] static float DeltaTime() noexcept;
        /// 起動からの総経過秒。未構築時は 0。
        [[nodiscard]] static double Time() noexcept;
        /// fixed update 間の補間係数 [0, 1)。未構築時は 0。
        [[nodiscard]] static float Alpha() noexcept;

    private:
        void Init();
        void MainLoop();
        void Shutdown();

        std::unique_ptr<Impl> m_pImpl;
        static Application* s_instance;
    };

    /// Game 側で実装必須。WinMain から呼ばれる。
    /// 戻り値が nullptr なら exit code -1 で WinMain は即終了する。
    [[nodiscard]] std::unique_ptr<Application> CreateApplication();
    [[nodiscard]] std::unique_ptr<Scene> CreateInitialScene();

} // namespace ns::app
