#include "Framework/App/Application.h"

#include "Framework/Core/Clock.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Platform/Input.h"
#include "Framework/Scene/RootScene.h"

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
        NS::Core::FrameTimer timer;
        std::unique_ptr<NS::Scene::RootScene> scene;
        bool valid = false;
        bool quitRequested = false;
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
        m_pImpl->timer.SetFixedDelta(m_pImpl->desc.fixedDelta);

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
        m_pImpl->window->SetResizeCallback([rendererPtr](int w, int h) { rendererPtr->Resize(w, h); });

        auto* impl = m_pImpl.get();
        // callback 内で RequestClose を呼ぶと PostMessage が WM_CLOSE を再投擲し、
        // PollMessages が永久に抜けなくなる。
        m_pImpl->window->SetCloseCallback([impl]() { impl->quitRequested = true; });

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

    int Application::Run(std::unique_ptr<NS::Scene::RootScene> initialScene)
    {
        if (!IsValid())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::App, "Application::Run: IsValid()==false で起動拒否");
            return -1;
        }
        if (!initialScene)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::App, "Application::Run: initialScene が nullptr");
            return -1;
        }
        m_pImpl->scene = std::move(initialScene);

        Init();
        MainLoop();
        Shutdown();
        return 0;
    }

    void Application::Init()
    {
        DrainPendingQuit();
        m_pImpl->timer.Reset();
        m_pImpl->scene->OnStart();
    }

    void Application::MainLoop()
    {
        auto& window = *m_pImpl->window;
        auto& renderer = *m_pImpl->renderer;
        auto& input = *m_pImpl->input;
        auto& timer = m_pImpl->timer;
        auto& scene = *m_pImpl->scene;
        const auto& desc = m_pImpl->desc;

        while (!window.ShouldClose() && !m_pImpl->quitRequested)
        {
            window.PollMessages();
            if (window.ShouldClose() || m_pImpl->quitRequested)
                break;

            timer.Tick();

            const int steps = timer.FixedStepsThisFrame();
            const float fixedDt = timer.FixedDelta();

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
                    scene.OnUpdate(fixedDt);
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
            scene.OnRender();
            renderer.EndFrame();
        }
    }

    void Application::Shutdown()
    {
        if (m_pImpl->scene)
            m_pImpl->scene->OnShutdown();
        m_pImpl->scene.reset();

        // Window が生存中にコールバックが発火すると rendererPtr / impl 生キャプチャが
        // 解放済みになるリスクがあるため、Renderer / Input を破棄する前に Window 側の
        // コールバックを全て nullptr に解除する。
        if (m_pImpl->window)
        {
            m_pImpl->window->SetResizeCallback(nullptr);
            m_pImpl->window->SetCloseCallback(nullptr);
            m_pImpl->window->AttachInput(nullptr);
        }
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

    float Application::DeltaTime() noexcept
    {
        if (s_instance == nullptr || !s_instance->m_pImpl)
            return 0.0f;
        return s_instance->m_pImpl->timer.DeltaSeconds();
    }

    double Application::Time() noexcept
    {
        if (s_instance == nullptr || !s_instance->m_pImpl)
            return 0.0;
        return s_instance->m_pImpl->timer.TotalSeconds();
    }

    float Application::Alpha() noexcept
    {
        if (s_instance == nullptr || !s_instance->m_pImpl)
            return 0.0f;
        return s_instance->m_pImpl->timer.Alpha();
    }

} // namespace NS::App
