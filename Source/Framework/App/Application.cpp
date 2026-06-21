#include "Framework/App/Application.h"

#include "Framework/App/Layer.h"
#include "Framework/Core/Clock.h"
#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Platform/Input.h"
#include "Framework/Scene/AssetManager.h"

#include "Framework/Framework.h"

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
            // 落とす (単一保持を Shipping でも保証)
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

        m_input = std::make_unique<NS::Platform::Input>();
        m_window->AttachInput(m_input.get());

        // リサイズ購読は Renderer が ctor で自己登録済 (swapchain 再構築はレンダラの責務)

        // WM_CLOSE 再投擲による PollMessages 無限ループを防ぐためフラグ経由でメインループに委譲
        m_window->SetCloseCallback([this]() { m_quitRequested = true; });

        // ImGui / 編集 UI の駆動は EditorLayer (overlay) が握る。App は UI を知らない (出荷で UI 層を外せる)

        m_valid = true;
    }

    Application::~Application()
    {
        // Run() を経由しないケースでも dangling キャプチャを防ぐため Shutdown() を呼ぶ。冪等
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
        return *m_input;
    }

    NS::Scene::AssetManager& Application::Assets() noexcept
    {
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

        // Renderer は ctor で構築済。各 scene の OnAttach/OnStart が builtin や共有 material を引く前に用意する
        // 共有 material は builtin の shader/texture を借りるため RegisterBuiltins の後に組む
        m_assets = std::make_unique<NS::Scene::AssetManager>(NS::Core::FileSystem::ContentRoot());
        m_assets->RegisterBuiltins();
        m_assets->RegisterSharedMaterials();

        for (auto& layer : m_layers)
            layer->OnAttach();
    }

    void Application::MainLoop()
    {
        auto& window = *m_window;
        auto& renderer = *m_renderer;
        auto& input = *m_input;
        auto& stack = m_layers;

        while (!window.ShouldClose() && !m_quitRequested)
        {
            window.PollMessages();
            if (window.ShouldClose() || m_quitRequested)
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
                    if (m_quitRequested)
                        break;
                }
            }
            if (m_quitRequested)
                break;

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

        // Layer の OnDetach は逆順 (top → bottom) で呼ぶ
        for (auto it = m_layers.rbegin(); it != m_layers.rend(); ++it)
            (*it)->OnDetach();

        // 破棄前に自分が登録したコールバックを解除し、Window 側の発火で dangling ポインタを踏むのを防ぐ
        // リサイズ購読は Renderer 自身が dtor で解除する
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
        m_input.reset();
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

} // namespace NS::App
