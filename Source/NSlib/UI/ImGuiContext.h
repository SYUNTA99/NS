#pragma once

#include "NSlib/Core/NonCopyable.h"

#include "NSlib/Core/Types.h"
#include <memory>

namespace NS
{
    namespace OS
    {
        class Window;
    } // namespace OS
} // namespace NS

namespace NS
{
    namespace Gfx
    {
        class Renderer;
    } // namespace Gfx
} // namespace NS

namespace NS
{
    namespace UI
    {
        //! ウィンドウと Renderer に対応する開発用 ImGui。無効な構成では何もしない
        class ImGuiContext : public NS::NonCopyable
        {
        public:
            struct Impl;

            //! 指定されたウィンドウと描画システムを用いてUIコンテキストを初期化する
            ImGuiContext(NS::OS::Window& window, NS::Gfx::Renderer& renderer) noexcept;
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
                                              NS::uint32 msg,
                                              NS::uintptr wParam,
                                              NS::intptr lParam) noexcept;

            //! UI ウィンドウを操作中などで、UI がマウス入力を要求しているかを返す
            [[nodiscard]] bool WantCaptureMouse() const noexcept;

            //! テキストボックスへの入力中などで、UI がキーボード入力を要求しているかを返す
            [[nodiscard]] bool WantCaptureKeyboard() const noexcept;

        private:
            std::unique_ptr<Impl> m_pImpl;
        };
    } // namespace UI
} // namespace NS
