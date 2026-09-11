#pragma once

#include "Runtime/Core/NonCopyable.h"

#include <cstdint>
#include <memory>

namespace NS::Platform
{
    class Window;
}

namespace NS::Graphics
{
    class Renderer;
}

namespace NS::UI
{

    //! @brief デバッグ・エディタ用の ImGui の寿命
    //! @details ウィンドウと Renderer に紐付き、初期化から毎フレームの描画までを持つ
    //! ImGui を外したビルドでは何もしないスタブになる
    class ImGuiContext : public NS::Core::NonCopyable
    {
    public:
        struct Impl;

        //! 指定されたウィンドウと描画システムを用いてUIコンテキストを初期化する
        ImGuiContext(NS::Platform::Window& window, NS::Graphics::Renderer& renderer) noexcept;
        ~ImGuiContext() noexcept;

        [[nodiscard]] bool IsValid() const noexcept;

        //! リリースビルドか初期化失敗でスタブになっている場合 true、それ以外の場合は false
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        //! @brief 新しいUIフレームの構築を開始する
        //! @note Renderer::BeginFrame の直後に呼ぶこと
        void BeginFrame() noexcept;

        //! @brief UIの構築を終了し、描画コマンドを発行する
        //! @note Renderer::EndFrame の直前に呼ぶこと
        void EndFrame() noexcept;

        //! @brief OSからのウィンドウメッセージをUI側へ転送して処理する
        //! @return UI がメッセージを消費した場合 true、それ以外の場合は false。true ならゲーム側は入力を無視する
        [[nodiscard]] bool ForwardWndProc(void* hwnd,
                                          std::uint32_t msg,
                                          std::uintptr_t wParam,
                                          std::intptr_t lParam) noexcept;

        //! UI ウィンドウを操作中などで、UI がマウス入力を要求しているかを返す
        [[nodiscard]] bool WantCaptureMouse() const noexcept;

        //! テキストボックスへの入力中などで、UI がキーボード入力を要求しているかを返す
        [[nodiscard]] bool WantCaptureKeyboard() const noexcept;

    private:
        std::unique_ptr<Impl> m_pImpl;
    };

} // namespace NS::UI
