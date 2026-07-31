#include "Runtime/UI/ImGuiContext.h"

#include "Runtime/Core/Filesystem.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/GraphicObject.h"
#include "Runtime/Graphics/Renderer.h"
#include "Runtime/Platform/Window.h"

#include <memory>

#include <windows.h>

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
        bool valid = false;   // 初期化に成功したか
        bool fallback = true; // スタブ / 失敗で実機能が無効か
#if NS_UI_IMGUI_ENABLED
        ::ImGuiContext* context = nullptr;      // ImGui コンテキスト (所有)
        NS::Platform::Window* window = nullptr; // カーソル状態参照用 (非所有)
#endif
    };

#if NS_UI_IMGUI_ENABLED
    namespace
    {
        // エディタ用の暗い配色に整える。 中間グレー地・角を落としたフラット・青の選択色へ寄せる
        // 明るいメニューバーとドロップダウンは描画時に個別上書きするので、 ここでは触れない
        void ApplyEditorStyle() noexcept
        {
            ImGuiStyle& style = ::ImGui::GetStyle();

            style.WindowRounding = 0.0f;
            style.ChildRounding = 0.0f;
            style.FrameRounding = 0.0f;
            style.PopupRounding = 0.0f;
            style.ScrollbarRounding = 0.0f;
            style.GrabRounding = 0.0f;
            style.TabRounding = 0.0f;
            style.WindowBorderSize = 1.0f;
            style.FrameBorderSize = 0.0f;
            style.TabBorderSize = 0.0f;

            style.WindowPadding = ImVec2{6.0f, 6.0f};
            style.FramePadding = ImVec2{6.0f, 3.0f};
            style.ItemSpacing = ImVec2{6.0f, 4.0f};
            style.IndentSpacing = 16.0f;
            style.ScrollbarSize = 13.0f;
            style.GrabMinSize = 8.0f;

            const ImVec4 panel{0.22f, 0.22f, 0.22f, 1.0f};  // パネル地
            const ImVec4 dark{0.16f, 0.16f, 0.16f, 1.0f};   // 入力欄・見出し・タブ地
            const ImVec4 hover{0.28f, 0.28f, 0.28f, 1.0f};  // hover
            const ImVec4 accent{0.22f, 0.42f, 0.65f, 1.0f}; // 選択の青
            const ImVec4 accentDim{0.19f, 0.33f, 0.48f, 1.0f};
            const ImVec4 text{0.83f, 0.83f, 0.83f, 1.0f};

            ImVec4* c = style.Colors;
            c[ImGuiCol_Text] = text;
            c[ImGuiCol_TextDisabled] = ImVec4{0.50f, 0.50f, 0.50f, 1.0f};
            c[ImGuiCol_WindowBg] = panel;
            c[ImGuiCol_ChildBg] = panel;
            c[ImGuiCol_PopupBg] = ImVec4{0.20f, 0.20f, 0.20f, 1.0f};
            c[ImGuiCol_Border] = ImVec4{0.13f, 0.13f, 0.13f, 1.0f};
            c[ImGuiCol_BorderShadow] = ImVec4{0.0f, 0.0f, 0.0f, 0.0f};
            c[ImGuiCol_FrameBg] = dark;
            c[ImGuiCol_FrameBgHovered] = hover;
            c[ImGuiCol_FrameBgActive] = accentDim;
            c[ImGuiCol_TitleBg] = dark;
            c[ImGuiCol_TitleBgActive] = dark;
            c[ImGuiCol_TitleBgCollapsed] = dark;
            c[ImGuiCol_MenuBarBg] = panel;
            c[ImGuiCol_ScrollbarBg] = dark;
            c[ImGuiCol_ScrollbarGrab] = ImVec4{0.35f, 0.35f, 0.35f, 1.0f};
            c[ImGuiCol_ScrollbarGrabHovered] = ImVec4{0.42f, 0.42f, 0.42f, 1.0f};
            c[ImGuiCol_ScrollbarGrabActive] = ImVec4{0.50f, 0.50f, 0.50f, 1.0f};
            c[ImGuiCol_CheckMark] = ImVec4{0.90f, 0.90f, 0.90f, 1.0f};
            c[ImGuiCol_SliderGrab] = ImVec4{0.55f, 0.55f, 0.55f, 1.0f};
            c[ImGuiCol_SliderGrabActive] = accent;
            c[ImGuiCol_Button] = ImVec4{0.30f, 0.30f, 0.30f, 1.0f};
            c[ImGuiCol_ButtonHovered] = ImVec4{0.37f, 0.37f, 0.37f, 1.0f};
            c[ImGuiCol_ButtonActive] = accent;
            c[ImGuiCol_Header] = accentDim;
            c[ImGuiCol_HeaderHovered] = hover;
            c[ImGuiCol_HeaderActive] = accent;
            c[ImGuiCol_Separator] = ImVec4{0.13f, 0.13f, 0.13f, 1.0f};
            c[ImGuiCol_SeparatorHovered] = accentDim;
            c[ImGuiCol_SeparatorActive] = accent;
            c[ImGuiCol_ResizeGrip] = ImVec4{0.30f, 0.30f, 0.30f, 0.6f};
            c[ImGuiCol_ResizeGripHovered] = hover;
            c[ImGuiCol_ResizeGripActive] = accent;
            c[ImGuiCol_Tab] = dark;
            c[ImGuiCol_TabHovered] = hover;
            c[ImGuiCol_TabSelected] = panel;
            c[ImGuiCol_TabDimmed] = dark;
            c[ImGuiCol_TabDimmedSelected] = ImVec4{0.20f, 0.20f, 0.20f, 1.0f};
            c[ImGuiCol_DockingPreview] = accentDim;
            c[ImGuiCol_DockingEmptyBg] = dark;
        }
    } // namespace
#endif

    ImGuiContext::ImGuiContext(NS::Platform::Window& window, NS::Graphics::Renderer& renderer) noexcept
        : m_pImpl(std::make_unique<Impl>())
    {
#if NS_UI_IMGUI_ENABLED
        IMGUI_CHECKVERSION();
        m_pImpl->window = &window;

        m_pImpl->context = ::ImGui::CreateContext();
        if (m_pImpl->context == nullptr)
        {
            NS_LOG_ERROR(UI, "ImGui::CreateContext 失敗、 stub mode に fallback");
            return;
        }

        ::ImGuiIO& io = ::ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        // パネルをメインウィンドウの外へドラッグしても各自が OS ウィンドウとして描画され続ける
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        ::ImGui::StyleColorsDark();
        ApplyEditorStyle();
        // 別 OS ウィンドウは半透明だと背景が透けるので、 ビューポート有効時は確実に不透明へ
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            ::ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 1.0f;

        // 既定フォントは ASCII のみで日本語が ??? になるため、 システムの日本語フォントを読み込む
        // エディタは dev 専用なので Windows のフォントパス直指定でよい。 見つからなければ ASCII 既定で続行する
        {
            constexpr const char* k_FontCandidates[] = {
                "C:/Windows/Fonts/YuGothM.ttc",
                "C:/Windows/Fonts/meiryo.ttc",
                "C:/Windows/Fonts/msgothic.ttc",
            };
            // 日本語に加えて □ (パネル全面化ボタン) を出すため、 幾何学記号を 1 字だけ範囲へ足す
            static ImVector<ImWchar> s_glyphRanges;
            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
            builder.AddChar(static_cast<ImWchar>(0x25A1)); // □
            s_glyphRanges.clear();
            builder.BuildRanges(&s_glyphRanges);
            const ImWchar* ranges = s_glyphRanges.Data;
            for (const char* fontPath : k_FontCandidates)
            {
                if (!NS::Core::FileSystem::Exists(fontPath))
                    continue;
                if (io.Fonts->AddFontFromFileTTF(fontPath, 18.0f, nullptr, ranges) != nullptr)
                    break;
            }
            if (io.Fonts->Fonts.Size == 0)
                NS_LOG_WARN(UI, "日本語フォントが見つからず ASCII 既定で続行、 日本語は ??? 表示になる");
        }

        // Win32 backend 初期化
        HWND hwnd = reinterpret_cast<HWND>(window.NativeHandle());
        if (hwnd == nullptr || !::ImGui_ImplWin32_Init(hwnd))
        {
            NS_LOG_ERROR(UI, "ImGui_ImplWin32_Init 失敗、 stub mode に fallback");
            ::ImGui::DestroyContext(m_pImpl->context);
            m_pImpl->context = nullptr;
            return;
        }

        // DX11 backend 初期化
        (void)renderer;
        ID3D11Device* device = NS::Graphics::Gpu().device;
        ID3D11DeviceContext* context = NS::Graphics::Gpu().context;
        if (device == nullptr || context == nullptr || !::ImGui_ImplDX11_Init(device, context))
        {
            NS_LOG_ERROR(UI, "ImGui_ImplDX11_Init 失敗、 stub mode に fallback");
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
        // メインウィンドウの外へ出たパネルを、各 OS ウィンドウへ描画・present する
        if (::ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ::ImGui::UpdatePlatformWindows();
            ::ImGui::RenderPlatformWindowsDefault();
        }
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
