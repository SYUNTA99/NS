#include "Framework/UI/ImGuiContext.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include <windows.h>
#include "Framework/Graphics/GraphicObject.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Platform/Window.h"

#if NS_EDITOR_ENABLED
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <imgui.h>

// WndProcHandler は backend が非公開にしているため利用側で前方宣言する
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
        ::ImGuiContext* context = nullptr;
        NS::Platform::Window* window = nullptr;
#endif
    };

    ImGuiContext::ImGuiContext(NS::Platform::Window& window, NS::Graphics::Renderer& renderer) noexcept
        : m_pImpl(std::make_unique<Impl>())
    {
#if NS_UI_IMGUI_ENABLED
        IMGUI_CHECKVERSION();
        m_pImpl->window = &window;

        m_pImpl->context = ::ImGui::CreateContext();
        if (m_pImpl->context == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::UI, "ImGui::CreateContext 失敗、 stub mode に fallback");
            return;
        }

        ::ImGuiIO& io = ::ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ::ImGui::StyleColorsDark();

        // 既定フォントは ASCII のみで日本語が ??? になるため、 システムの日本語フォントを読み込む
        // editor は dev 専用なので Windows のフォントパス直指定でよい。 見つからなければ ASCII 既定で続行する
        {
            constexpr const char* kFontCandidates[] = {
                "C:/Windows/Fonts/YuGothM.ttc",
                "C:/Windows/Fonts/meiryo.ttc",
                "C:/Windows/Fonts/msgothic.ttc",
            };
            const ImWchar* ranges = io.Fonts->GetGlyphRangesJapanese();
            for (const char* fontPath : kFontCandidates)
            {
                if (!NS::Core::FileSystem::Exists(fontPath))
                    continue;
                if (io.Fonts->AddFontFromFileTTF(fontPath, 18.0f, nullptr, ranges) != nullptr)
                    break;
            }
            if (io.Fonts->Fonts.Size == 0)
                NS_LOG_WARN(::NS::Core::LogCat::UI,
                            "日本語フォントが見つからず ASCII 既定で続行、 日本語は ??? 表示になる");
        }

        HWND hwnd = reinterpret_cast<HWND>(window.NativeHandle());
        if (hwnd == nullptr || !::ImGui_ImplWin32_Init(hwnd))
        {
            NS_LOG_ERROR(::NS::Core::LogCat::UI, "ImGui_ImplWin32_Init 失敗、 stub mode に fallback");
            ::ImGui::DestroyContext(m_pImpl->context);
            m_pImpl->context = nullptr;
            return;
        }

        (void)renderer;
        ID3D11Device* device = NS::Graphics::Gpu().device;
        ID3D11DeviceContext* context = NS::Graphics::Gpu().context;
        if (device == nullptr || context == nullptr || !::ImGui_ImplDX11_Init(device, context))
        {
            NS_LOG_ERROR(::NS::Core::LogCat::UI, "ImGui_ImplDX11_Init 失敗、 stub mode に fallback");
            ::ImGui_ImplWin32_Shutdown();
            ::ImGui::DestroyContext(m_pImpl->context);
            m_pImpl->context = nullptr;
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
        if (m_pImpl && m_pImpl->context != nullptr)
        {
            ::ImGui_ImplDX11_Shutdown();
            ::ImGui_ImplWin32_Shutdown();
            ::ImGui::DestroyContext(m_pImpl->context);
            m_pImpl->context = nullptr;
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
        // カーソル非表示中は ImGui に OS カーソルを触らせず、 Window の SetCursor(nullptr) を保つ
        // これをしないと backend が毎フレーム矢印へ戻し、 プレイ中もカーソルが消えない
        ::ImGuiIO& io = ::ImGui::GetIO();
        if (m_pImpl->window != nullptr && !m_pImpl->window->IsCursorVisible())
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        else
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
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
