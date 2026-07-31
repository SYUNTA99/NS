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

    //! @brief デバッグ・エディタ用UI（Dear ImGui）のライフサイクルを管理するクラス。
    //! @details ウィンドウと描画システムに紐付き、UIの初期化から毎フレームの描画処理までを担う。
    //! リリースビルド時など、UI機能がシステムから除外されている場合は自動的にスタブとして振る舞う。
    class ImGuiContext : public NS::Core::NonCopyable
    {
    public:
        struct Impl;

        //! 指定されたウィンドウと描画システムを用いてUIコンテキストを初期化する
        ImGuiContext(NS::Platform::Window& window, NS::Graphics::Renderer& renderer) noexcept;
        ~ImGuiContext() noexcept;

        [[nodiscard]] bool IsValid() const noexcept;

        //! リリースビルドや初期化失敗により、UI機能が無効化（スタブ化）されていればtrueを返す
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        //! @brief 新しいUIフレームの構築を開始する
        //! @note 描画システムのフレーム開始（Renderer::BeginFrame）の直後に呼び出すこと
        void BeginFrame() noexcept;

        //! @brief UIの構築を終了し、描画コマンドを発行する
        //! @note 描画システムのフレーム終了（Renderer::EndFrame）の直前に呼び出すこと
        void EndFrame() noexcept;

        //! @brief OSからのウィンドウメッセージをUI側へ転送して処理する
        //! @return UIがメッセージを消費した場合（ゲーム側で入力を無視すべき場合）はtrueを返す
        [[nodiscard]] bool ForwardWndProc(void* hwnd,
                                          std::uint32_t msg,
                                          std::uintptr_t wParam,
                                          std::intptr_t lParam) noexcept;

        //! UIがマウス入力を要求しているか（UIウィンドウの操作中など）を返す
        [[nodiscard]] bool WantCaptureMouse() const noexcept;

        //! UIがキーボード入力を要求しているか（テキストボックスへの入力中など）を返す
        [[nodiscard]] bool WantCaptureKeyboard() const noexcept;

    private:
        std::unique_ptr<Impl> m_pImpl;
    };

} // namespace NS::UI
