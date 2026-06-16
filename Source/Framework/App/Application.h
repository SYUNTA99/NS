#pragma once

/// @file Application.h
/// @brief NS::App::Application — Window/Renderer/Input/Audio + Layers を所有し、メインループを駆動する
///
/// @details サブシステム (Window/Renderer/Input、Audio は placeholder) を所有し破棄まで管理する
/// `Run()` で Init → MainLoop → Shutdown を順に実行する。Layer 群は WinMain 段階で
/// AddLayer / AddOverlay で積み、Run ループ内では追加/削除しない (iterator が無効化されるため)
///
/// 多重起動禁止 (s_instance 単一保持で assert)。構築失敗時は `IsValid() == false` を返し、
/// `Run` は -1 で即終了。`Get` / `Quit` は未構築時に nullptr を返す (例外を投げない)
/// 時刻や補間 alpha が要るなら `NS::Core::FrameTimer` を直接呼ぶ

#include "Framework/App/Layers.h"
#include "Framework/Core/Clock.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Platform/Window.h"

#include <chrono>
#include <memory>

namespace NS::App
{
    class Layer;
}

namespace NS::Platform
{
    class Input;
}

namespace NS::UI
{
    class ImGuiContext;
}

namespace NS::App
{

    /// Application 構築パラメータ。Window / Renderer の Desc を内包する
    /// 描画のプロジェクト既定値は renderer.settings (RendererDesc) 側で設定する
    struct ApplicationDesc
    {
        NS::Platform::WindowDesc window{};
        NS::Graphics::RendererDesc renderer{};
        /// 固定 Update の delta 秒。 デフォルト 1/60
        float fixedDelta = NS::Core::FrameTimer::kDefaultFixedDelta;
    };

    /// サブシステムを所有しメインループを駆動する。Get()/Quit() でグローバルアクセス可。多重起動禁止
    class Application
    {
    public:
        explicit Application(const ApplicationDesc& desc);
        ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;
        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;

        /// 構築成功判定。Window / Renderer のいずれかが失敗していたら false
        [[nodiscard]] bool IsValid() const noexcept;

        /// regular layer を追加する (overlay より前)。Run の前に呼ぶこと (ループ中は iterator 無効化)
        void AddLayer(std::unique_ptr<NS::App::Layer> layer);

        /// overlay layer を追加する (regular より後ろ、OnRender が最後)。Run の前に呼ぶこと
        void AddOverlay(std::unique_ptr<NS::App::Layer> overlay);

        /// Init → MainLoop → Shutdown を実行する。IsValid()==false か layer が 0 個なら -1 で即終了
        int Run();

        [[nodiscard]] NS::Platform::Window& Window() noexcept;
        [[nodiscard]] NS::Graphics::Renderer& Renderer() noexcept;
        [[nodiscard]] NS::Platform::Input& Input() noexcept;

        /// Debug/Dev build のみ実体を持つ ImGuiContext。Shipping では nullptr
        [[nodiscard]] NS::UI::ImGuiContext* ImGui() noexcept;

        /// 現在の Application インスタンス。未構築時は nullptr
        [[nodiscard]] static Application* Get() noexcept;
        /// 次フレームの MainLoop ループ抜け要求。Get() が nullptr の場合は何もしない
        static void Quit() noexcept;

    private:
        void Init();
        void MainLoop();
        void Shutdown();

        ApplicationDesc m_desc;
        std::unique_ptr<NS::Platform::Window> m_window;
        std::unique_ptr<NS::Graphics::Renderer> m_renderer;
        std::unique_ptr<NS::Platform::Input> m_input;
        // Debug / Development build のみ実体化。GameDebug / GameRelease では常に nullptr
        std::unique_ptr<NS::UI::ImGuiContext> m_imgui;
        Layers m_layers;
        bool m_valid = false;
        bool m_quitRequested = false;
        // Run() とデストラクタ両経路で Shutdown が走ると Layer::OnDetach が二重呼びされるため防護
        bool m_shutdownCalled = false;
        std::chrono::steady_clock::time_point m_lastStutterWarnAt{};

        static Application* s_instance;
    };

    /// Game 側で実装必須。ApplicationDesc を構築し、Layer / overlay まで積んだ Application を返す
    /// (WinMain を Game / Editor へ依存させないため、Layer 構成は本関数側で完結させる)
    [[nodiscard]] std::unique_ptr<Application> CreateApplication();

} // namespace NS::App
