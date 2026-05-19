#include "ns/app/application.h"

#include "ns/app/scene.h"
#include "ns/core/clock.h"
#include "ns/core/log_categories.h"
#include "ns/core/logger.h"
#include "ns/platform/input.h"

#include <Windows.h>

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

namespace ns::app
{

    Application* Application::s_instance = nullptr;

    struct Application::Impl
    {
        ApplicationDesc desc;
        std::unique_ptr<ns::platform::Window> window;
        std::unique_ptr<ns::graphics::Renderer> renderer;
        std::unique_ptr<ns::platform::Input> input;
        ns::core::FrameTimer timer;
        std::unique_ptr<Scene> scene;
        bool valid = false;
        bool quitRequested = false;
    };

    Application::Application(const ApplicationDesc& desc) : m_pImpl(std::make_unique<Impl>())
    {
        if (s_instance != nullptr)
        {
            // assert は Release で消えるため、本制約は Release ビルドでも fatal で
            // 落とす ( 単一保持を Shipping でも保証)。
            NS_LOG_FATAL(::ns::core::LogCat::App, "Application 多重起動禁止 ()");
        }
        s_instance = this;

        m_pImpl->desc = desc;
        if (m_pImpl->desc.fixedDelta <= 0.0f)
        {
            NS_LOG_WARN(::ns::core::LogCat::App,
                        "ApplicationDesc::fixedDelta が非正値 ({}) のため default 1/60 にフォールバック",
                        m_pImpl->desc.fixedDelta);
            m_pImpl->desc.fixedDelta = 1.0f / 60.0f;
        }
        m_pImpl->timer.SetFixedDelta(m_pImpl->desc.fixedDelta);

        m_pImpl->window = std::make_unique<ns::platform::Window>(desc.window);
        if (!m_pImpl->window->IsValid())
        {
            NS_LOG_ERROR(::ns::core::LogCat::App, "Application: Window 構築失敗");
            return;
        }

        m_pImpl->renderer = std::make_unique<ns::graphics::Renderer>(desc.renderer, *m_pImpl->window);
        if (!m_pImpl->renderer->IsValid())
        {
            NS_LOG_ERROR(::ns::core::LogCat::App, "Application: Renderer 構築失敗");
            return;
        }

        m_pImpl->input = std::make_unique<ns::platform::Input>();
        m_pImpl->window->AttachInput(m_pImpl->input.get());

        auto* rendererPtr = m_pImpl->renderer.get();
        m_pImpl->window->SetResizeCallback([rendererPtr](int w, int h) { rendererPtr->Resize(w, h); });

        auto* impl = m_pImpl.get();
        // RequestClose は PostMessage(hwnd, WM_CLOSE) で WM_CLOSE を再投擲するため、
        // この callback が呼ばれた直後に PollMessages の while ループが新しい WM_CLOSE
        // を見つけ続けて永久に抜けなくなる。flag を立てるだけにし、PollMessages を
        // キュー空で抜けさせて MainLoop の while 条件で正常終了させる。
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

    ns::platform::Window& Application::Window() noexcept
    {
        return *m_pImpl->window;
    }

    ns::graphics::Renderer& Application::Renderer() noexcept
    {
        return *m_pImpl->renderer;
    }

    ns::platform::Input& Application::Input() noexcept
    {
        return *m_pImpl->input;
    }

    int Application::Run(std::unique_ptr<Scene> initialScene)
    {
        if (!IsValid())
        {
            NS_LOG_ERROR(::ns::core::LogCat::App, "Application::Run: IsValid()==false で起動拒否");
            return -1;
        }
        if (!initialScene)
        {
            NS_LOG_ERROR(::ns::core::LogCat::App, "Application::Run: initialScene が nullptr");
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

            input.Update();
            timer.Tick();

            const int steps = timer.FixedStepsThisFrame();
            const float fixedDt = timer.FixedDelta();
            for (int i = 0; i < steps; ++i)
            {
                scene.OnUpdate(fixedDt);
                if (m_pImpl->quitRequested)
                    break;
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

} // namespace ns::app
