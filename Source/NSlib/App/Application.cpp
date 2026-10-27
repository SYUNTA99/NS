#include "NSlib/App/Application.h"

#include "NSlib/App/Layer.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Windows/Input.h"

#include <chrono>
#include <memory>

#include <windows.h>

namespace
{
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

        m_device = std::make_unique<Gfx::D3DDevice>(desc.device, *m_window);
        if (!m_device->IsValid())
        {
            NS_LOG_ERROR(App, "Application: D3DDevice 構築失敗");
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

    bool Application::IsValid() const noexcept { return m_valid; }

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

    Gfx::D3DDevice& Application::Device() noexcept
    {
        NS_ASSERT(App, m_device, "構築の失敗後 / Shutdown 後に Device() を呼んでいる");
        return *m_device;
    }

    const Gfx::D3DDevice& Application::Device() const noexcept
    {
        NS_ASSERT(App, m_device, "構築の失敗後 / Shutdown 後に Device() を呼んでいる");
        return *m_device;
    }

    OS::Input& Application::Input() noexcept { return OS::Input::Get(); }

    const OS::Input& Application::Input() const noexcept { return OS::Input::Get(); }

    void Application::SetCursorCaptured(bool captured) noexcept
    {
        m_window->SetCursorVisible(!captured);
        // 固定しないとクリックが他のパネルへ落ち、押しっぱなしの体当たり入力が届かないフレームができる
        m_window->SetCursorLocked(captured);
        OS::Input::Get().SetMouseRelativeMode(captured);
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
        m_assetsScope = std::make_unique<AssetManagerScope>(*m_assets);

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
        Gfx::D3DDevice& device = *m_device;
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
                    // 固定更新のたびにマウスの移動量とホイールを区切り、パッドを読み直す
                    // 押した瞬間は、読む物がそれぞれ自分の InputReader で読む
                    input.Update();
                }
            }

            device.BeginFrame();

            for (std::unique_ptr<Layer>& layer : stack)
            {
                if (layer->IsActive())
                {
                    layer->OnRender();
                }
            }

            device.EndFrame();
        }
    }

    void Application::Shutdown()
    {
        if (m_shutdownCalled)
        {
            return;
        }

        m_shutdownCalled = true;

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
        if (m_assets)
        {
            m_assets->Clear();
        }
        m_device.reset();
        m_window.reset();
    }

    Application* Application::Get() noexcept { return s_instance; }

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
