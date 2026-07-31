#include "Runtime/App/Application.h"

#include "Runtime/App/Layer.h"
#include "Runtime/Core/Assert.h"
#include "Runtime/Core/Clock.h"
#include "Runtime/Core/Filesystem.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Object/AssetManager.h"
#include "Runtime/Platform/Input.h"

#include <memory>

#include <windows.h>

namespace
{
    // 前回残ってしまった終了メッセージを捨てておく
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

    Application::Application(const ApplicationDesc& desc)
    {
        if (s_instance != nullptr)
        {
            NS_LOG_FATAL(App, "Application 多重起動禁止");
        }
        s_instance = this;

        m_desc = desc;
        if (m_desc.fixedDelta <= 0.0f)
        {
            NS_LOG_WARN(App,
                        "ApplicationDesc::fixedDelta が非正値 ({}) のため default 1/60 にフォールバック",
                        m_desc.fixedDelta);
            m_desc.fixedDelta = NS::Core::FrameTimer::k_DefaultFixedDelta;
        }
        NS::Core::FrameTimer::SetFixedDelta(m_desc.fixedDelta);

        m_window = std::make_unique<NS::Platform::Window>(desc.window);
        if (!m_window->IsValid())
        {
            NS_LOG_ERROR(App, "Application: Window 構築失敗");
            return;
        }

        m_renderer = std::make_unique<NS::Graphics::Renderer>(desc.renderer, *m_window);
        if (!m_renderer->IsValid())
        {
            NS_LOG_ERROR(App, "Application: Renderer 構築失敗");
            return;
        }

        // ウィンドウに入力処理を登録する
        m_window->AttachInput(&NS::Platform::Input::Get());

        // ウィンドウが閉じられたら終了フラグを立てる
        m_window->SetCloseCallback([this]() { m_quitRequested = true; });

        m_valid = true;
    }

    Application::~Application()
    {
        // 念のため終了処理を呼んでおく
        Shutdown();
        if (s_instance == this)
            s_instance = nullptr;
    }

    bool Application::IsValid() const noexcept
    {
        return m_valid;
    }

    NS::Platform::Window& Application::Window() noexcept
    {
        return *m_window;
    }

    NS::Graphics::Renderer& Application::Renderer() noexcept
    {
        return *m_renderer;
    }

    NS::Platform::Input& Application::Input() noexcept
    {
        return NS::Platform::Input::Get();
    }

    NS::Object::AssetManager& Application::Assets() noexcept
    {
        NS_ASSERT(App, m_assets, "Init 前 / Shutdown 後に Assets() を呼んでいる");
        return *m_assets;
    }

    void Application::AddLayer(std::unique_ptr<NS::App::Layer> layer)
    {
        m_layers.AddLayer(std::move(layer));
    }

    void Application::AddOverlay(std::unique_ptr<NS::App::Layer> overlay)
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
        NS::Core::FrameTimer::Reset();

        // アセットマネージャーを準備する
        m_assets = std::make_unique<NS::Object::AssetManager>(NS::Core::FileSystem::ContentRoot());
        m_assets->RegisterBuiltins();
        m_assets->RegisterSharedMaterials();

        for (auto& layer : m_layers)
            layer->OnAttach();
    }

    bool Application::WantExit() noexcept
    {
        // ウィンドウが閉じられていたら終了する
        if (m_window->ShouldClose())
            return true;
        if (!m_quitRequested)
            return false;
        if (m_quitGuard)
        {
            // guard 呼出は try 内の 1 回だけにする。 noexcept 境界で例外を外へ出すと std::terminate になる
            try
            {
                if (!m_quitGuard())
                {
                    m_quitRequested = false;
                    return false;
                }
            }
            catch (...)
            {
                // 例外が飛んだらログを出して、安全のため終了処理自体を一旦キャンセル（ループ継続）する
                NS_LOG_ERROR(App, "コールバック内で例外が発生しました");
                m_quitRequested = false;
                return false;
            }
        }
        return true;
    }

    void Application::MainLoop()
    {
        auto& window = *m_window;
        auto& renderer = *m_renderer;
        auto& input = NS::Platform::Input::Get();
        auto& stack = m_layers;

        while (true)
        {
            window.PollMessages();

            // 終了すべきかチェックする
            if (WantExit())
                break;

            NS::Core::FrameTimer::Tick();

            const int steps = NS::Core::FrameTimer::FixedStepsThisFrame();

            // 1フレームの更新が多すぎる場合は警告を出す
            if (steps >= 2)
            {
                const auto now = std::chrono::steady_clock::now();
                const auto since =
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
                    for (auto& layer : stack)
                    {
                        if (layer->IsActive())
                            layer->OnUpdate();
                    }
                    // 入力状態を更新する
                    input.Update();
                }
            }

            // 描画
            renderer.BeginFrame();

            for (auto& layer : stack)
            {
                if (layer->IsActive())
                    layer->OnRender();
            }

            renderer.EndFrame();
        }
    }

    void Application::Shutdown()
    {
        if (m_shutdownCalled)
            return;
        m_shutdownCalled = true;

        // 追加したのと逆の順番でレイヤーを終了させる
        for (auto it = m_layers.rbegin(); it != m_layers.rend(); ++it)
            (*it)->OnDetach();

        // 終了ガードをクリアする
        m_quitGuard = nullptr;

        // エラーを防ぐために登録したコールバックを解除しておく
        if (m_window)
        {
            m_window->SetCloseCallback(nullptr);
            m_window->AttachInput(nullptr);
        }

        // レンダラーより先にアセットマネージャーを解放する
        if (m_assets)
            m_assets->Clear();
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
            return;
        s_instance->m_quitRequested = true;
    }

    void Application::SetQuitGuard(std::function<bool()> guard) noexcept
    {
        m_quitGuard = std::move(guard);
    }

} // namespace NS::App