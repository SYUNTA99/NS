#include "Framework/Graphics/Renderer.h"

#include "Framework/Graphics/Buffer.h"
#include "Framework/Graphics/CommonStates.h"
#include "Framework/Graphics/Shader.h"
#include "Framework/Graphics/Texture.h"
#include "Framework/Graphics/TextureArray.h"
#include "Framework/Graphics/detail/d3d_context.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <cassert>
#include <iterator>

namespace NS::Graphics
{
    using detail::ComPtr;

    struct Renderer::Impl
    {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<IDXGISwapChain> swapchain;

        std::unique_ptr<Texture> backbuffer;
        std::unique_ptr<Texture> depth;
        std::unique_ptr<CommonStates> states;

        ::NS::Platform::Window* window = nullptr;
        bool vsync = true;
        bool valid = false;
    };

    namespace
    {
        bool TryCreateDeviceAndSwapChain(HWND hwnd,
                                         int width,
                                         int height,
                                         UINT createFlags,
                                         ComPtr<ID3D11Device>& outDevice,
                                         ComPtr<ID3D11DeviceContext>& outContext,
                                         ComPtr<IDXGISwapChain>& outSwapchain)
        {
            DXGI_SWAP_CHAIN_DESC scd{};
            scd.BufferCount = 1;
            scd.BufferDesc.Width = static_cast<UINT>(width);
            scd.BufferDesc.Height = static_cast<UINT>(height);
            scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            scd.BufferDesc.RefreshRate.Numerator = 60;
            scd.BufferDesc.RefreshRate.Denominator = 1;
            scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            scd.OutputWindow = hwnd;
            scd.SampleDesc.Count = 1;
            scd.SampleDesc.Quality = 0;
            scd.Windowed = TRUE;
            scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

            const D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_0};
            D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_11_0;

            const HRESULT hr = ::D3D11CreateDeviceAndSwapChain(nullptr,
                                                               D3D_DRIVER_TYPE_HARDWARE,
                                                               nullptr,
                                                               createFlags,
                                                               featureLevels,
                                                               static_cast<UINT>(std::size(featureLevels)),
                                                               D3D11_SDK_VERSION,
                                                               &scd,
                                                               outSwapchain.GetAddressOf(),
                                                               outDevice.GetAddressOf(),
                                                               &got,
                                                               outContext.GetAddressOf());

            if (FAILED(hr))
            {
                NS_LOG_WARN(::NS::Core::LogCat::Graphics,
                            "D3D11CreateDeviceAndSwapChain failed (hr=0x{:X}, flags=0x{:X})",
                            static_cast<unsigned>(hr),
                            static_cast<unsigned>(createFlags));
                return false;
            }
            return true;
        }

        void SuppressAltEnter(IDXGISwapChain* swapchain, HWND hwnd)
        {
            ComPtr<IDXGIDevice> dxgiDevice;
            ComPtr<IDXGIAdapter> adapter;
            ComPtr<IDXGIFactory> factory;
            ComPtr<ID3D11Device> dev;

            if (swapchain == nullptr || hwnd == nullptr)
            {
                return;
            }
            HRESULT hr = swapchain->GetDevice(IID_PPV_ARGS(dev.GetAddressOf()));
            if (FAILED(hr))
            {
                return;
            }
            hr = dev.As(&dxgiDevice);
            if (FAILED(hr))
            {
                return;
            }
            hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
            if (FAILED(hr))
            {
                return;
            }
            hr = adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
            if (FAILED(hr))
            {
                return;
            }
            factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        }

        // swapchain の backbuffer を RTV Texture として包み、 同サイズの depth Texture を生成する
        // 構築 / Resize の両方から呼ぶ。 いずれか失敗で false (out は未確定)
        bool BuildBackbufferTargets(Renderer& renderer,
                                    IDXGISwapChain* swapchain,
                                    std::unique_ptr<Texture>& outBackbuffer,
                                    std::unique_ptr<Texture>& outDepth)
        {
            ComPtr<ID3D11Texture2D> bb;
            const HRESULT hr = swapchain->GetBuffer(0, IID_PPV_ARGS(bb.GetAddressOf()));
            if (FAILED(hr))
            {
                NS_LOG_ERROR(
                    ::NS::Core::LogCat::Graphics, "SwapChain::GetBuffer 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
                return false;
            }

            auto backbuffer = std::make_unique<Texture>(renderer, bb, D3D11_BIND_RENDER_TARGET);
            if (backbuffer->Rtv() == nullptr)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "backbuffer RTV の構築失敗");
                return false;
            }

            const ::NS::Math::Size2D size = backbuffer->Size();
            TextureCreateDesc depthDesc{};
            depthDesc.width = static_cast<UINT>(size.width);
            depthDesc.height = static_cast<UINT>(size.height);
            depthDesc.format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            depthDesc.bindFlags = D3D11_BIND_DEPTH_STENCIL;

            auto depth = std::make_unique<Texture>(renderer, depthDesc);
            if (depth->Dsv() == nullptr)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "depth DSV の構築失敗");
                return false;
            }

            outBackbuffer = std::move(backbuffer);
            outDepth = std::move(depth);
            return true;
        }
    } // namespace

    Renderer::Renderer(const RendererDesc& desc, ::NS::Platform::Window& window) : m_pImpl(std::make_unique<Impl>())
    {
        m_pImpl->window = &window;
        m_pImpl->vsync = desc.vsync;

        if (!window.IsValid())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "Renderer 構築時に Window が無効");
            return;
        }

        HWND hwnd = reinterpret_cast<HWND>(window.NativeHandle());
        const ::NS::Math::Size2D winSize = window.Size();
        const int w = winSize.width;
        const int h = winSize.height;

        UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        if (desc.enableDebugLayer)
        {
            createFlags |= D3D11_CREATE_DEVICE_DEBUG;
        }

        bool ok =
            TryCreateDeviceAndSwapChain(hwnd, w, h, createFlags, m_pImpl->device, m_pImpl->context, m_pImpl->swapchain);
        if (!ok && desc.enableDebugLayer)
        {
            NS_LOG_WARN(::NS::Core::LogCat::Graphics,
                        "Debug Layer 付きでの D3D11 デバイス作成に失敗、Debug Layer 無しで再試行");
            createFlags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
            ok = TryCreateDeviceAndSwapChain(
                hwnd, w, h, createFlags, m_pImpl->device, m_pImpl->context, m_pImpl->swapchain);
        }
        if (!ok)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "D3D11 デバイス作成失敗");
            return;
        }

        SuppressAltEnter(m_pImpl->swapchain.Get(), hwnd);

        if (!BuildBackbufferTargets(*this, m_pImpl->swapchain.Get(), m_pImpl->backbuffer, m_pImpl->depth))
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "backbuffer / depth Texture の構築失敗");
            return;
        }

        m_pImpl->states.reset(new CommonStates(static_cast<void*>(m_pImpl->device.Get())));

        window.SetResizeCallback([this](::NS::Math::Size2D rs) { this->Resize(rs); });

        m_pImpl->valid = true;
        // 構築完了は通常運用では成功が想定 (失敗時のみ別途 ERROR ログ済) なので Debug 段
        // test loop で per-fixture に renderer が立ち上がる時の log 雑音を抑える
        NS_LOG_DEBUG(::NS::Core::LogCat::Graphics,
                     "Renderer 構築完了 ({}x{}, vsync={}, debugLayer={})",
                     w,
                     h,
                     desc.vsync,
                     desc.enableDebugLayer);
    }

    Renderer::~Renderer()
    {
        if (m_pImpl && m_pImpl->context)
        {
            m_pImpl->context->ClearState();
            m_pImpl->context->Flush();
        }
    }

    bool Renderer::IsValid() const noexcept
    {
        return m_pImpl != nullptr && m_pImpl->valid;
    }

    void Renderer::BeginFrame(float r, float g, float b, float a) noexcept
    {
        if (!IsValid() || !m_pImpl->backbuffer || !m_pImpl->depth)
        {
            return;
        }
        auto* context = m_pImpl->context.Get();
        ID3D11RenderTargetView* rtv = m_pImpl->backbuffer->Rtv();
        ID3D11DepthStencilView* dsv = m_pImpl->depth->Dsv();

        const float color[4] = {r, g, b, a};
        context->ClearRenderTargetView(rtv, color);
        context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        ID3D11RenderTargetView* rtvs[1] = {rtv};
        context->OMSetRenderTargets(1, rtvs, dsv);

        const ::NS::Math::Size2D size = m_pImpl->backbuffer->Size();
        D3D11_VIEWPORT vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = static_cast<float>(size.width);
        vp.Height = static_cast<float>(size.height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        context->RSSetViewports(1, &vp);
    }

    void Renderer::EndFrame() noexcept
    {
        if (!IsValid() || !m_pImpl->swapchain)
        {
            return;
        }
        const UINT sync = m_pImpl->vsync ? 1 : 0;
        m_pImpl->swapchain->Present(sync, 0);
    }

    void Renderer::Resize(::NS::Math::Size2D size) noexcept
    {
        if (!IsValid())
        {
            return;
        }
        if (size.width <= 0 || size.height <= 0)
        {
            return;
        }

        auto* context = m_pImpl->context.Get();
        // ResizeBuffers の前に backbuffer 参照を全て手放す (RTV を握ったままだと失敗する)
        context->OMSetRenderTargets(0, nullptr, nullptr);
        m_pImpl->backbuffer.reset();
        m_pImpl->depth.reset();
        context->ClearState();
        context->Flush();

        const HRESULT hr = m_pImpl->swapchain->ResizeBuffers(
            0, static_cast<UINT>(size.width), static_cast<UINT>(size.height), DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr))
        {
            NS_LOG_ERROR(
                ::NS::Core::LogCat::Graphics, "SwapChain::ResizeBuffers 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
            return;
        }

        BuildBackbufferTargets(*this, m_pImpl->swapchain.Get(), m_pImpl->backbuffer, m_pImpl->depth);
    }

    ::NS::Math::Size2D Renderer::Size() const noexcept
    {
        if (!m_pImpl || !m_pImpl->backbuffer)
        {
            return ::NS::Math::Size2D{0, 0};
        }
        return m_pImpl->backbuffer->Size();
    }

    CommonStates& Renderer::States() noexcept
    {
        assert(m_pImpl && m_pImpl->states && "Renderer が無効な状態で States() を呼んでいる");
        return *m_pImpl->states;
    }

    ID3D11Device* Renderer::NativeDevice() noexcept
    {
        return detail::GetDevice(*this);
    }

    ID3D11DeviceContext* Renderer::NativeContext() noexcept
    {
        return detail::GetContext(*this);
    }

    void Renderer::BindShader(Shader& shader) noexcept
    {
        if (m_pImpl)
        {
            detail::BindShader(m_pImpl->context.Get(), shader);
        }
    }
    void Renderer::BindTexture(const Texture& texture, unsigned slot, ShaderStage stages) noexcept
    {
        if (m_pImpl)
        {
            detail::BindTexture(m_pImpl->context.Get(), texture, slot, stages);
        }
    }
    void Renderer::BindTextureArray(TextureArray& texture, unsigned slot, ShaderStage stages) noexcept
    {
        if (m_pImpl)
        {
            detail::BindTextureArray(m_pImpl->context.Get(), texture, slot, stages);
        }
    }
    void Renderer::BindVertexBuffer(Buffer& vertexBuffer, unsigned slot) noexcept
    {
        if (m_pImpl)
        {
            detail::BindVertexBuffer(m_pImpl->context.Get(), vertexBuffer, slot);
        }
    }
    void Renderer::BindIndexBuffer(Buffer& indexBuffer) noexcept
    {
        if (m_pImpl)
        {
            detail::BindIndexBuffer(m_pImpl->context.Get(), indexBuffer);
        }
    }
    void Renderer::BindConstantBuffer(Buffer& constantBuffer, unsigned slot, ShaderStage stages) noexcept
    {
        if (m_pImpl)
        {
            detail::BindConstantBuffer(m_pImpl->context.Get(), constantBuffer, slot, stages);
        }
    }
    void Renderer::UpdateBuffer(Buffer& buffer, const void* data, std::size_t bytes) noexcept
    {
        if (m_pImpl)
        {
            detail::UpdateBufferRaw(m_pImpl->context.Get(), buffer, data, bytes);
        }
    }
    void Renderer::DrawIndexed(unsigned indexCount) noexcept
    {
        if (m_pImpl && m_pImpl->context)
        {
            m_pImpl->context->DrawIndexed(indexCount, 0u, 0);
        }
    }

    namespace detail
    {
        ID3D11Device* GetDevice(Renderer& renderer) noexcept
        {
            return renderer.m_pImpl ? renderer.m_pImpl->device.Get() : nullptr;
        }

        ID3D11DeviceContext* GetContext(Renderer& renderer) noexcept
        {
            return renderer.m_pImpl ? renderer.m_pImpl->context.Get() : nullptr;
        }

        IDXGISwapChain* GetSwapChain(Renderer& renderer) noexcept
        {
            return renderer.m_pImpl ? renderer.m_pImpl->swapchain.Get() : nullptr;
        }
    } // namespace detail

} // namespace NS::Graphics
