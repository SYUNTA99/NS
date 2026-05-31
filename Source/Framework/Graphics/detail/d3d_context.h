#pragma once

/// graphics 層内部限定 (detail/ 配下)。
/// <windows.h> / <d3d11.h> / <dxgi.h> / <wrl/client.h> をここに集約し、
/// 公開ヘッダから D3D11 / Win32 シンボルが漏れないようにする。
///
/// 使い方:
///   #include "Framework/Graphics/detail/d3d_context.h"
///   auto* dev = ::NS::Graphics::detail::GetDevice(renderer);
///
/// public header (renderer.h / render_target.h / common_states.h) からは
/// 絶対に include しないこと (grep 検証対象)。

#include "Framework/Framework.h"
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

namespace NS::Graphics
{
    class Renderer;

    namespace detail
    {
        template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

        /// Renderer が所有する D3D11 Device を取得 (typed friend accessor)。
        /// Buffer / Texture / Shader が Device を必要とするときに呼ぶ。
        [[nodiscard]] ID3D11Device* GetDevice(Renderer& renderer) noexcept;

        /// Renderer が所有する D3D11 DeviceContext を取得。
        [[nodiscard]] ID3D11DeviceContext* GetContext(Renderer& renderer) noexcept;

        /// Renderer が所有する DXGI SwapChain を取得 (RenderTarget::Resize で使用)。
        [[nodiscard]] IDXGISwapChain* GetSwapChain(Renderer& renderer) noexcept;
    } // namespace detail
} // namespace NS::Graphics

// Buffer 系の detail::GetNative は buffer.h で宣言されている。
// 利用側は #include "Framework/Graphics/Buffer.h" を別途行うこと。
