#include "Framework/UI/ImGuiContext.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Framework.h"
#include "Framework/Graphics/GraphicObject.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Platform/Window.h"

#if defined(NS_BUILD_DEBUG) || defined(NS_BUILD_DEV)
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <imgui.h>

// imgui_impl_win32.h は <windows.h> 依存を避けるため WndProcHandler を `#if 0`
// ブロックで公開していない。 利用側で forward declare してから呼ぶのが backend の規約
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#define NS_UI_IMGUI_ENABLED 1
#else
#define NS_UI_IMGUI_ENABLED 0
#endif

namespace NS::UI
{

    struct ImGuiContext::Impl
    {
        bool valid = false;
        bool fallback = true;
#if NS_UI_IMGUI_ENABLED
        ::ImGuiContext* ctx = nullptr;
#endif
    };

    ImGuiContext::ImGuiContext(NS::Platform::Window& window, NS::Graphics::Renderer& renderer) noexcept
        : m_pImpl(std::make_unique<Impl>())
    {
#if NS_UI_IMGUI_ENABLED
        IMGUI_CHECKVERSION();

        m_pImpl->ctx = ::ImGui::CreateContext();
        if (m_pImpl->ctx == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::UI, "ImGui::CreateContext 失敗、 stub mode に fallback");
            return;
        }

        ::ImGuiIO& io = ::ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ::ImGui::StyleColorsDark();

        HWND hwnd = reinterpret_cast<HWND>(window.NativeHandle());
        if (hwnd == nullptr || !::ImGui_ImplWin32_Init(hwnd))
        {
            NS_LOG_ERROR(::NS::Core::LogCat::UI, "ImGui_ImplWin32_Init 失敗、 stub mode に fallback");
            ::ImGui::DestroyContext(m_pImpl->ctx);
            m_pImpl->ctx = nullptr;
            return;
        }

        (void)renderer;
        ID3D11Device* device = NS::Graphics::Gpu().device;
        ID3D11DeviceContext* context = NS::Graphics::Gpu().context;
        if (device == nullptr || context == nullptr || !::ImGui_ImplDX11_Init(device, context))
        {
            NS_LOG_ERROR(::NS::Core::LogCat::UI, "ImGui_ImplDX11_Init 失敗、 stub mode に fallback");
            ::ImGui_ImplWin32_Shutdown();
            ::ImGui::DestroyContext(m_pImpl->ctx);
            m_pImpl->ctx = nullptr;
            return;
        }

        m_pImpl->valid = true;
        m_pImpl->fallback = false;
#else
        (void)window;
        (void)renderer;
#endif
    }

    ImGuiContext::~ImGuiContext() noexcept
    {
#if NS_UI_IMGUI_ENABLED
        if (m_pImpl && m_pImpl->ctx != nullptr)
        {
            ::ImGui_ImplDX11_Shutdown();
            ::ImGui_ImplWin32_Shutdown();
            ::ImGui::DestroyContext(m_pImpl->ctx);
            m_pImpl->ctx = nullptr;
        }
#endif
    }

    bool ImGuiContext::IsValid() const noexcept
    {
        return m_pImpl && m_pImpl->valid;
    }

    bool ImGuiContext::IsUsingFallback() const noexcept
    {
        return !m_pImpl || m_pImpl->fallback;
    }

    void ImGuiContext::BeginFrame() noexcept
    {
#if NS_UI_IMGUI_ENABLED
        if (!IsValid())
            return;
        ::ImGui_ImplDX11_NewFrame();
        ::ImGui_ImplWin32_NewFrame();
        ::ImGui::NewFrame();
#endif
    }

    void ImGuiContext::EndFrame() noexcept
    {
#if NS_UI_IMGUI_ENABLED
        if (!IsValid())
            return;
        ::ImGui::Render();
        ::ImGui_ImplDX11_RenderDrawData(::ImGui::GetDrawData());
#endif
    }

    bool ImGuiContext::ForwardWndProc(void* hwnd,
                                      std::uint32_t msg,
                                      std::uintptr_t wParam,
                                      std::intptr_t lParam) noexcept
    {
#if NS_UI_IMGUI_ENABLED
        if (!IsValid())
            return false;
        return ::ImGui_ImplWin32_WndProcHandler(reinterpret_cast<HWND>(hwnd),
                                                static_cast<UINT>(msg),
                                                static_cast<WPARAM>(wParam),
                                                static_cast<LPARAM>(lParam)) != 0;
#else
        (void)hwnd;
        (void)msg;
        (void)wParam;
        (void)lParam;
        return false;
#endif
    }

    bool ImGuiContext::WantCaptureMouse() const noexcept
    {
#if NS_UI_IMGUI_ENABLED
        if (!IsValid())
            return false;
        return ::ImGui::GetIO().WantCaptureMouse;
#else
        return false;
#endif
    }

    bool ImGuiContext::WantCaptureKeyboard() const noexcept
    {
#if NS_UI_IMGUI_ENABLED
        if (!IsValid())
            return false;
        return ::ImGui::GetIO().WantCaptureKeyboard;
#else
        return false;
#endif
    }

} // namespace NS::UI
