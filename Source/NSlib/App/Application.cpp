#include "NSlib/App/Application.h"

#include "NSlib/App/Layer.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/Windows/Clock.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/Input.h"

#include <chrono>
#include <memory>

#include <windows.h>

namespace
{
    // 前回残ってしまった終了メッセージを捨てておく
    void DrainPendingQuit() noexcept
    {
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {}
    }
} // namespace

namespace NS
{

    Application* Application::s_instance = nullptr;

    Application::Application(const ApplicationDesc& desc)
    {
        if (s_instance != nullptr)
        {
            NS_LOG_FATAL(App, "Application 多重起動禁止");
        }
        s_instance = this;

        float fixedDelta = desc.fixedDelta;
        if (fixedDelta <= 0.0f)
        {
            NS_LOG_WARN(
                App, "ApplicationDesc::fixedDelta が非正値 ({}) のため default 1/60 にフォールバック", fixedDelta);
            fixedDelta = OS::FrameTimer::k_DefaultFixedDelta;
        }
        OS::FrameTimer::SetFixedDelta(fixedDelta);

        m_window = std::make_unique<OS::Window>(desc.window);
        if (!m_window->IsValid())
        {
            NS_LOG_ERROR(App, "Application: Window 構築失敗");
            return;
        }

        m_renderer = std::make_unique<Gfx::Renderer>(desc.renderer, *m_window);
        if (!m_renderer->IsValid())
        {
            NS_LOG_ERROR(App, "Application: Renderer 構築失敗");
            return;
        }

        m_window->AttachInput(&OS::Input::Get());

        m_window->SetCloseCallback([this]() { m_quitRequested = true; });

        m_valid = true;
    }

    Application::~Application()
    {
        Shutdown();
        if (s_instance == this)
        {
            s_instance = nullptr;
        }
    }

    bool Application::IsValid() const noexcept
    {
        return m_valid;
    }

    OS::Window& Application::Window() noexcept
    {
        NS_ASSERT(App, m_window, "構築の失敗後 / Shutdown 後に Window() を呼んでいる");
        return *m_window;
    }

    const OS::Window& Application::Window() const noexcept
    {
        NS_ASSERT(App, m_window, "構築の失敗後 / Shutdown 後に Window() を呼んでいる");
        return *m_window;
    }

    Gfx::Renderer& Application::Renderer() noexcept
    {
        NS_ASSERT(App, m_renderer, "構築の失敗後 / Shutdown 後に Renderer() を呼んでいる");
        return *m_renderer;
    }

    const Gfx::Renderer& Application::Renderer() const noexcept
    {
        NS_ASSERT(App, m_renderer, "構築の失敗後 / Shutdown 後に Renderer() を呼んでいる");
        return *m_renderer;
    }

    OS::Input& Application::Input() noexcept
    {
        return OS::Input::Get();
    }

    const OS::Input& Application::Input() const noexcept
    {
        return OS::Input::Get();
    }

    void Application::SetCursorCaptured(bool captured) noexcept
    {
        m_window->SetCursorVisible(!captured);
        // 固定しないとクリックが他のパネルへ落ち、押しっぱなしの体当たり入力が届かないフレームができる
        m_window->SetCursorLocked(captured);
        OS::Input::Get().Mouse().SetRelativeMode(captured);
    }

    AssetManager& Application::Assets() noexcept
    {
        NS_ASSERT(App, m_assets, "Init 前 / Shutdown 後に Assets() を呼んでいる");
        return *m_assets;
    }

    const AssetManager& Application::Assets() const noexcept
    {
        NS_ASSERT(App, m_assets, "Init 前 / Shutdown 後に Assets() を呼んでいる");
        return *m_assets;
    }

    void Application::AddLayer(std::unique_ptr<Layer> layer)
    {
        m_layers.AddLayer(std::move(layer));
    }

    void Application::AddOverlay(std::unique_ptr<Layer> overlay)
    {
        m_layers.AddOverlay(std::move(overlay));
    }

    int Application::Run()
    {
        if (!IsValid())
        {
            NS_LOG_ERROR(App, "Application::Run: IsValid()==false で起動拒否");
            return -1;
        }
        if (m_layers.Empty())
        {
            NS_LOG_ERROR(App, "Application::Run: layer が 1 個も追加されていません");
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
        OS::FrameTimer::Reset();

        m_assets = std::make_unique<AssetManager>(OS::FileSystem::ContentRoot());
        m_assets->RegisterBuiltins();
        m_assetsScope = std::make_unique<AssetManagerScope>(*m_assets);
        // Renderer は AssetManager より先に立つので、組み込みのシェーダーは AssetManager を作った後に渡す
        m_renderer->SetShaders(m_assets->LoadBuiltinShaders());

        for (std::unique_ptr<Layer>& layer : m_layers)
        {
            layer->OnAttach();
        }
        m_attached = true;
    }

    bool Application::WantExit() noexcept
    {
        if (m_window->ShouldClose())
        {
            return true;
        }
        if (!m_quitRequested)
        {
            return false;
        }

        if (m_quitGuard && !m_quitGuard())
        {
            m_quitRequested = false;
            return false;
        }
        return true;
    }

    void Application::MainLoop()
    {
        OS::Window& window = *m_window;
        Gfx::Renderer& renderer = *m_renderer;
        OS::Input& input = OS::Input::Get();
        Layers& stack = m_layers;

        while (true)
        {
            window.PollMessages();

            if (WantExit())
            {
                break;
            }

            OS::FrameTimer::Tick();

            const int steps = OS::FrameTimer::FixedStepsThisFrame();

            if (steps >= 2)
            {
                const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
                const std::chrono::milliseconds::rep since =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastStutterWarnAt).count();
                if (since >= 1000)
                {
                    NS_LOG_WARN(App, "Frame drop indicator: {} fixed steps in single frame", steps);
                    m_lastStutterWarnAt = now;
                }
            }

            {
                NS_SCOPED_TIMER(App, "Application::FixedStepLoop");
                for (int i = 0; i < steps; ++i)
                {
                    for (std::unique_ptr<Layer>& layer : stack)
                    {
                        if (layer->IsActive())
                        {
                            layer->OnUpdate();
                        }
                    }
                    // 固定更新のたびに基準を進める。描画フレーム単位だと、
                    // 1 フレームに 2 回更新が入った時に 2 回とも「押した瞬間」になる
                    input.Update();
                }
            }

            renderer.BeginFrame();

            for (std::unique_ptr<Layer>& layer : stack)
            {
                if (layer->IsActive())
                {
                    layer->OnRender();
                }
            }

            renderer.EndFrame();
        }
    }

    void Application::Shutdown()
    {
        if (m_shutdownCalled)
        {
            return;
        }

        m_shutdownCalled = true;

        // 起動を断った時は OnAttach が走っていない
        if (m_attached)
        {
            for (std::vector<std::unique_ptr<Layer>>::reverse_iterator it = m_layers.rbegin(); it != m_layers.rend();
                 ++it)
            {
                (*it)->OnDetach();
            }
        }

        // 終了の後は Update が来ないので、止めないと最後に送った振動が実機に残る
        OS::Input::Get().Gamepad().StopVibration();

        m_quitGuard = nullptr;

        if (m_window)
        {
            m_window->SetCloseCallback(nullptr);
            m_window->AttachInput(nullptr);
        }

        m_assetsScope.reset();
        // Clear が捨てるシェーダーを Renderer に指させたままにしない
        if (m_renderer)
        {
            m_renderer->SetShaders(Gfx::BuiltinShaders{});
        }
        if (m_assets)
        {
            m_assets->Clear();
        }
        m_renderer.reset();
        m_window.reset();
    }

    Application* Application::Get() noexcept
    {
        return s_instance;
    }

    void Application::Quit() noexcept
    {
        if (s_instance == nullptr)
        {
            return;
        }
        s_instance->m_quitRequested = true;
    }

    void Application::SetQuitGuard(std::function<bool()> guard) noexcept
    {
        m_quitGuard = std::move(guard);
    }

} // namespace NS
