#include "Framework/App/Application.h"

#include "Framework/App/Layer.h"
#include "Framework/Core/Clock.h"
#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Platform/Input.h"
#include "Framework/Scene/AssetManager.h"

#include "Framework/Framework.h"

#include <cassert>
#include <chrono>
#include <utility>

namespace
{
    // 前回の PostQuitMessage 残留が次回起動の PollMessages で ShouldClose=true にならないよう捨てる
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
            // assert は Release で消えるため、本制約は Release ビルドでも fatal で
            // 落とし、単一保持を出荷ビルドでも保証する
            NS_LOG_FATAL(::NS::Core::LogCat::App, "Application 多重起動禁止");
        }
        s_instance = this;

        m_desc = desc;
        if (m_desc.fixedDelta <= 0.0f)
        {
            NS_LOG_WARN(::NS::Core::LogCat::App,
                        "ApplicationDesc::fixedDelta が非正値 ({}) のため default 1/60 にフォールバック",
                        m_desc.fixedDelta);
            m_desc.fixedDelta = NS::Core::FrameTimer::kDefaultFixedDelta;
        }
        NS::Core::FrameTimer::SetFixedDelta(m_desc.fixedDelta);

        m_window = std::make_unique<NS::Platform::Window>(desc.window);
        if (!m_window->IsValid())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::App, "Application: Window 構築失敗");
            return;
        }

        m_renderer = std::make_unique<NS::Graphics::Renderer>(desc.renderer, *m_window);
        if (!m_renderer->IsValid())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::App, "Application: Renderer 構築失敗");
            return;
        }

        // 入力はプロセス全域の static。Application は所有せず Window の転送先として渡すだけ
        m_window->AttachInput(&NS::Platform::Input::Get());

        // リサイズ購読は Renderer がコンストラクタで自己登録済。swapchain 再構築はレンダラの責務

        // WM_CLOSE 再投擲による PollMessages 無限ループを防ぐためフラグ経由でメインループに委譲
        m_window->SetCloseCallback([this]() { m_quitRequested = true; });

        // ImGui / 編集 UI の駆動は overlay の EditorLayer が握る。App は UI を知らず出荷で UI 層を外せる

        m_valid = true;
    }

    Application::~Application()
    {
        // Run() を経由しないケースでも無効参照のキャプチャを防ぐため Shutdown() を呼ぶ。冪等
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

    NS::Scene::AssetManager& Application::Assets() noexcept
    {
        assert(m_assets && "Init 前 / Shutdown 後に Assets() を呼んでいる");
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
            NS_LOG_ERROR(::NS::Core::LogCat::App, "Application::Run: IsValid()==false で起動拒否");
            return -1;
        }
        if (m_layers.Empty())
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

        // Renderer はコンストラクタで構築済。各 scene の OnAttach/OnStart が組み込みや共有 material を引く前に用意する
        // 共有 material は組み込みの shader/texture を借りるため RegisterBuiltins の後に組む
        m_assets = std::make_unique<NS::Scene::AssetManager>(NS::Core::FileSystem::ContentRoot());
        m_assets->RegisterBuiltins();
        m_assets->RegisterSharedMaterials();

        for (auto& layer : m_layers)
            layer->OnAttach();
    }

    bool Application::WantExit() noexcept
    {
        // window 破棄経由の WM_QUIT は guard で覆せないので即終了する
        if (m_window->ShouldClose())
            return true;
        if (!m_quitRequested)
            return false;
        if (m_quitGuard && !m_quitGuard())
        {
            m_quitRequested = false;
            return false;
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
            // 終了判定は WantExit に集約する
            if (WantExit())
                break;

            NS::Core::FrameTimer::Tick();

            const int steps = NS::Core::FrameTimer::FixedStepsThisFrame();

            if (steps >= 2)
            {
                const auto now = std::chrono::steady_clock::now();
                const auto since =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastStutterWarnAt).count();
                if (since >= 1000)
                {
                    NS_LOG_WARN(::NS::Core::LogCat::App, "Frame drop indicator: {} fixed steps in single frame", steps);
                    m_lastStutterWarnAt = now;
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
                    // fixed step ごとに Update して edge 重複検出を防ぐ
                    input.Update();
                }
            }

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

        // Layer の OnDetach は top → bottom の逆順で呼ぶ
        for (auto it = m_layers.rbegin(); it != m_layers.rend(); ++it)
            (*it)->OnDetach();

        // guard が捕捉する Layer は OnDetach 済。 発火経路を断ってから subsystem を畳む
        m_quitGuard = nullptr;

        // 破棄前に自分が登録したコールバックを解除し、Window 側の発火で無効ポインタを踏むのを防ぐ
        // リサイズ購読は Renderer 自身がデストラクタで解除する
        if (m_window)
        {
            m_window->SetCloseCallback(nullptr);
            m_window->AttachInput(nullptr);
            // ImGui の message hook 解除と context 破棄は EditorLayer::OnDetach が先に済ませている
        }
        // AssetManager は GPU リソースを握るため Renderer 破棄より前に解放する
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
