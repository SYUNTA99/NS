#include "Framework/App/Application.h"

#include "Framework/App/Layer.h"
#include "Framework/App/Layers.h"
#include "Framework/Core/Clock.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Platform/Input.h"
#include "Framework/UI/ImGuiContext.h"

#include "Framework/Framework.h"

#include <chrono>
#include <utility>

namespace
{
    /// 前 Application 終了時の Window destruction で PostQuitMessage の thread quit flag
    /// が残ることがある (Win32 仕様、PeekMessageW の filter range では取れない)。
    /// 同一プロセスで Application を再起動した時に初回 PollMessages で即 ShouldClose=true
    /// になるのを防ぐため、起動時に全メッセージを排出する。前 Application の HWND は
    /// すでに破棄済みなので Dispatch は呼ばず破棄のみで足りる。
    void DrainPendingQuit() noexcept
    {
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            (void)msg;
        }
    }
} // namespace

namespace NS::App
{

    Application* Application::s_instance = nullptr;

    struct Application::Impl
    {
        ApplicationDesc desc;
        std::unique_ptr<NS::Platform::Window> window;
        std::unique_ptr<NS::Graphics::Renderer> renderer;
        std::unique_ptr<NS::Platform::Input> input;
        /// Debug / Development build のみ実体化される。
        /// GameDebug / GameRelease では常に nullptr (ImGui 非搭載 shipping を保証)。
        std::unique_ptr<NS::UI::ImGuiContext> imgui;
        Layers layers;
        bool valid = false;
        bool quitRequested = false;
        // Run() 経由の Shutdown と、 dtor 経由の Shutdown が両方走った時に OnDetach が二重に
        // 呼ばれるのを防ぐ flag。 Window / Renderer の reset は unique_ptr で冪等だが、 Layer
        // 側 OnDetach は副作用を持ち得るため、 ここで明示的に guard する。
        bool shutdownCalled = false;
        std::chrono::steady_clock::time_point lastStutterWarnAt{};
    };

    Application::Application(const ApplicationDesc& desc) : m_pImpl(std::make_unique<Impl>())
    {
        if (s_instance != nullptr)
        {
            // assert は Release で消えるため、本制約は Release ビルドでも fatal で
            // 落とす ( 単一保持を Shipping でも保証)。
            NS_LOG_FATAL(::NS::Core::LogCat::App, "Application 多重起動禁止 ()");
        }
        s_instance = this;

        m_pImpl->desc = desc;
        if (m_pImpl->desc.fixedDelta <= 0.0f)
        {
            NS_LOG_WARN(::NS::Core::LogCat::App,
                        "ApplicationDesc::fixedDelta が非正値 ({}) のため default 1/60 にフォールバック",
                        m_pImpl->desc.fixedDelta);
            m_pImpl->desc.fixedDelta = 1.0f / 60.0f;
        }
        NS::Core::FrameTimer::SetFixedDelta(m_pImpl->desc.fixedDelta);

        m_pImpl->window = std::make_unique<NS::Platform::Window>(desc.window);
        if (!m_pImpl->window->IsValid())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::App, "Application: Window 構築失敗");
            return;
        }

        m_pImpl->renderer = std::make_unique<NS::Graphics::Renderer>(desc.renderer, *m_pImpl->window);
        if (!m_pImpl->renderer->IsValid())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::App, "Application: Renderer 構築失敗");
            return;
        }

        m_pImpl->input = std::make_unique<NS::Platform::Input>();
        m_pImpl->window->AttachInput(m_pImpl->input.get());

        auto* rendererPtr = m_pImpl->renderer.get();
        m_pImpl->window->SetResizeCallback([rendererPtr](::NS::Core::Size2D s) { rendererPtr->Resize(s); });

        auto* impl = m_pImpl.get();
        // callback 内で RequestClose を呼ぶと PostMessage が WM_CLOSE を再投擲し、
        // PollMessages が永久に抜けなくなる。
        m_pImpl->window->SetCloseCallback([impl]() { impl->quitRequested = true; });

#if defined(NS_BUILD_DEBUG) || defined(NS_BUILD_DEV)
        m_pImpl->imgui = std::make_unique<NS::UI::ImGuiContext>(*m_pImpl->window, *m_pImpl->renderer);
        if (!m_pImpl->imgui->IsValid())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::App, "ImGuiContext 構築失敗、 ImGui 機能は無効");
        }
        m_pImpl->window->AttachImGui(m_pImpl->imgui.get());
#endif

        m_pImpl->valid = true;
    }

    Application::~Application()
    {
        // Run() が呼ばれず Shutdown() を経由しないケース (構築失敗 / IsValid チェック
        // のみのテスト等) でも、Window 破壊前に callback を nullptr 化し
        // Renderer / Input を先に破棄して dangling キャプチャを防ぐ。
        // Shutdown() は冪等のため Run() 経由ケースでは no-op になる。
        if (m_pImpl)
            Shutdown();
        if (s_instance == this)
            s_instance = nullptr;
    }

    bool Application::IsValid() const noexcept
    {
        return m_pImpl && m_pImpl->valid;
    }

    NS::Platform::Window& Application::Window() noexcept
    {
        return *m_pImpl->window;
    }

    NS::Graphics::Renderer& Application::Renderer() noexcept
    {
        return *m_pImpl->renderer;
    }

    NS::Platform::Input& Application::Input() noexcept
    {
        return *m_pImpl->input;
    }

    NS::UI::ImGuiContext* Application::ImGui() noexcept
    {
        return m_pImpl ? m_pImpl->imgui.get() : nullptr;
    }

    void Application::AddLayer(std::unique_ptr<NS::App::Layer> layer)
    {
        if (!m_pImpl)
            return;
        m_pImpl->layers.AddLayer(std::move(layer));
    }

    void Application::AddOverlay(std::unique_ptr<NS::App::Layer> overlay)
    {
        if (!m_pImpl)
            return;
        m_pImpl->layers.AddOverlay(std::move(overlay));
    }

    int Application::Run()
    {
        if (!IsValid())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::App, "Application::Run: IsValid()==false で起動拒否");
            return -1;
        }
        if (m_pImpl->layers.Empty())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::App, "Application::Run: layer が 1 個も追加されていません");
            return -1;
        }

        Init();
        MainLoop();
        Shutdown();
        return 0;
    }

    void Application::Init()
    {
        DrainPendingQuit();
        NS::Core::FrameTimer::Reset();
        for (auto& layer : m_pImpl->layers)
            layer->OnAttach();
    }

    void Application::MainLoop()
    {
        auto& window = *m_pImpl->window;
        auto& renderer = *m_pImpl->renderer;
        auto& input = *m_pImpl->input;
        auto& stack = m_pImpl->layers;
        const auto& desc = m_pImpl->desc;

        while (!window.ShouldClose() && !m_pImpl->quitRequested)
        {
            window.PollMessages();
            if (window.ShouldClose() || m_pImpl->quitRequested)
                break;

            NS::Core::FrameTimer::Tick();

            const int steps = NS::Core::FrameTimer::FixedStepsThisFrame();

            if (steps >= 2)
            {
                const auto now = std::chrono::steady_clock::now();
                const auto since =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - m_pImpl->lastStutterWarnAt).count();
                if (since >= 1000)
                {
                    NS_LOG_WARN(::NS::Core::LogCat::App, "Frame drop indicator: {} fixed steps in single frame", steps);
                    m_pImpl->lastStutterWarnAt = now;
                }
            }

            {
                NS_SCOPED_TIMER(::NS::Core::LogCat::App, "Application::FixedStepLoop");
                for (int i = 0; i < steps; ++i)
                {
                    for (auto& layer : stack)
                    {
                        if (layer->IsActive())
                            layer->OnUpdate();
                    }
                    // fixed step ごとに input.Update を呼ぶことで、1 frame に複数 step
                    // 走った時に同じ edge が複数回検出されるのを防ぐ。
                    // 参考: https://jakubtomsu.github.io/posts/input_in_fixed_timestep/
                    input.Update();
                    if (m_pImpl->quitRequested)
                        break;
                }
            }
            if (m_pImpl->quitRequested)
                break;

            renderer.BeginFrame(desc.clearR, desc.clearG, desc.clearB, desc.clearA);
            if (m_pImpl->imgui)
                m_pImpl->imgui->BeginFrame();

            for (auto& layer : stack)
            {
                if (layer->IsActive())
                    layer->OnRender();
            }

            if (m_pImpl->imgui)
                m_pImpl->imgui->EndFrame();
            renderer.EndFrame();
        }
    }

    void Application::Shutdown()
    {
        if (m_pImpl->shutdownCalled)
            return;
        m_pImpl->shutdownCalled = true;

        // Layer の OnDetach は逆順 (top → bottom) で呼ぶ
        for (auto it = m_pImpl->layers.rbegin(); it != m_pImpl->layers.rend(); ++it)
            (*it)->OnDetach();

        // Window が生存中にコールバックが発火すると rendererPtr / impl 生キャプチャが
        // 解放済みになるリスクがあるため、Renderer / Input を破棄する前に Window 側の
        // コールバックを全て nullptr に解除する。
        if (m_pImpl->window)
        {
            m_pImpl->window->SetResizeCallback(nullptr);
            m_pImpl->window->SetCloseCallback(nullptr);
            m_pImpl->window->AttachInput(nullptr);
            m_pImpl->window->AttachImGui(nullptr);
        }
        // ImGui_ImplDX11_Shutdown は ID3D11Device を要求するので Renderer より先に破棄。
        m_pImpl->imgui.reset();
        m_pImpl->renderer.reset();
        m_pImpl->input.reset();
        m_pImpl->window.reset();
    }

    Application* Application::Get() noexcept
    {
        return s_instance;
    }

    void Application::Quit() noexcept
    {
        if (s_instance == nullptr || !s_instance->m_pImpl)
            return;
        s_instance->m_pImpl->quitRequested = true;
    }

} // namespace NS::App
