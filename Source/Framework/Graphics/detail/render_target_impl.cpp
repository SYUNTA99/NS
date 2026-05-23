#include "Framework/Graphics/RenderTarget.h"

#include "Framework/Graphics/detail/d3d_context.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

namespace NS::Graphics
{
    using detail::ComPtr;

    struct RenderTarget::Impl
    {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<IDXGISwapChain> swapchain;

        ComPtr<ID3D11RenderTargetView> rtv;
        ComPtr<ID3D11Texture2D> depthTex;
        ComPtr<ID3D11DepthStencilView> dsv;

        ::NS::Core::Size2D size{0, 0};
        bool hasDepth = false;
    };

    namespace
    {
        bool AcquireBackbufferRtv(IDXGISwapChain* swapchain,
                                  ID3D11Device* device,
                                  ComPtr<ID3D11RenderTargetView>& outRtv,
                                  int& outWidth,
                                  int& outHeight)
        {
            ComPtr<ID3D11Texture2D> backbuffer;
            HRESULT hr = swapchain->GetBuffer(0, IID_PPV_ARGS(backbuffer.GetAddressOf()));
            if (FAILED(hr))
            {
                NS_LOG_ERROR(
                    ::NS::Core::LogCat::Graphics, "SwapChain::GetBuffer 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
                return false;
            }

            hr = device->CreateRenderTargetView(backbuffer.Get(), nullptr, outRtv.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(
                    ::NS::Core::LogCat::Graphics, "CreateRenderTargetView 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
                return false;
            }

            D3D11_TEXTURE2D_DESC bbDesc{};
            backbuffer->GetDesc(&bbDesc);
            outWidth = static_cast<int>(bbDesc.Width);
            outHeight = static_cast<int>(bbDesc.Height);
            return true;
        }

        bool CreateDepthStencil(ID3D11Device* device,
                                int width,
                                int height,
                                ComPtr<ID3D11Texture2D>& outTex,
                                ComPtr<ID3D11DepthStencilView>& outDsv)
        {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = static_cast<UINT>(width);
            td.Height = static_cast<UINT>(height);
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            td.SampleDesc.Count = 1;
            td.SampleDesc.Quality = 0;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_DEPTH_STENCIL;

            HRESULT hr = device->CreateTexture2D(&td, nullptr, outTex.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(
                    ::NS::Core::LogCat::Graphics, "Depth texture 作成失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
                return false;
            }

            D3D11_DEPTH_STENCIL_VIEW_DESC dsd{};
            dsd.Format = td.Format;
            dsd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            dsd.Texture2D.MipSlice = 0;

            hr = device->CreateDepthStencilView(outTex.Get(), &dsd, outDsv.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(
                    ::NS::Core::LogCat::Graphics, "CreateDepthStencilView 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
                return false;
            }
            return true;
        }
    } // namespace

    RenderTarget::RenderTarget() : m_pImpl(std::make_unique<Impl>()) {}

    RenderTarget::~RenderTarget() = default;

    bool RenderTarget::ConfigureAsBackbuffer(IDXGISwapChain* swapchain,
                                             ID3D11Device* device,
                                             ID3D11DeviceContext* context,
                                             bool createDepth) noexcept
    {
        if (swapchain == nullptr || device == nullptr || context == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "RenderTarget::ConfigureAsBackbuffer: 必須引数が null (swapchain={}, device={}, context={})",
                         static_cast<const void*>(swapchain),
                         static_cast<const void*>(device),
                         static_cast<const void*>(context));
            return false;
        }

        m_pImpl->swapchain = swapchain;
        m_pImpl->device = device;
        m_pImpl->context = context;
        m_pImpl->hasDepth = createDepth;

        int w = 0;
        int h = 0;
        if (!AcquireBackbufferRtv(swapchain, device, m_pImpl->rtv, w, h))
        {
            return false;
        }
        m_pImpl->size = ::NS::Core::Size2D{w, h};

        if (createDepth)
        {
            if (!CreateDepthStencil(device, w, h, m_pImpl->depthTex, m_pImpl->dsv))
            {
                return false;
            }
        }
        return true;
    }

    void RenderTarget::Clear(float r, float g, float b, float a, float depth) noexcept
    {
        if (!m_pImpl->context)
        {
            return;
        }
        if (m_pImpl->rtv)
        {
            const float color[4] = {r, g, b, a};
            m_pImpl->context->ClearRenderTargetView(m_pImpl->rtv.Get(), color);
        }
        if (m_pImpl->dsv)
        {
            m_pImpl->context->ClearDepthStencilView(
                m_pImpl->dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depth, 0);
        }
    }

    void RenderTarget::Bind() noexcept
    {
        if (!m_pImpl->context || !m_pImpl->rtv)
        {
            return;
        }
        ID3D11RenderTargetView* rtvs[1] = {m_pImpl->rtv.Get()};
        m_pImpl->context->OMSetRenderTargets(1, rtvs, m_pImpl->dsv.Get());

        D3D11_VIEWPORT vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = static_cast<float>(m_pImpl->size.width);
        vp.Height = static_cast<float>(m_pImpl->size.height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        m_pImpl->context->RSSetViewports(1, &vp);
    }

    void RenderTarget::Resize(::NS::Core::Size2D size) noexcept
    {
        if (size.width <= 0 || size.height <= 0)
        {
            return;
        }
        if (!m_pImpl->swapchain || !m_pImpl->device || !m_pImpl->context)
        {
            return;
        }

        m_pImpl->context->OMSetRenderTargets(0, nullptr, nullptr);
        m_pImpl->rtv.Reset();
        m_pImpl->dsv.Reset();
        m_pImpl->depthTex.Reset();
        m_pImpl->context->ClearState();
        m_pImpl->context->Flush();

        HRESULT hr = m_pImpl->swapchain->ResizeBuffers(
            0, static_cast<UINT>(size.width), static_cast<UINT>(size.height), DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr))
        {
            NS_LOG_ERROR(
                ::NS::Core::LogCat::Graphics, "SwapChain::ResizeBuffers 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
            return;
        }

        int newW = 0;
        int newH = 0;
        if (!AcquireBackbufferRtv(m_pImpl->swapchain.Get(), m_pImpl->device.Get(), m_pImpl->rtv, newW, newH))
        {
            return;
        }
        m_pImpl->size = ::NS::Core::Size2D{newW, newH};

        if (m_pImpl->hasDepth)
        {
            CreateDepthStencil(m_pImpl->device.Get(), newW, newH, m_pImpl->depthTex, m_pImpl->dsv);
        }
    }

    ::NS::Core::Size2D RenderTarget::Size() const noexcept
    {
        return m_pImpl->size;
    }
    bool RenderTarget::HasDepth() const noexcept
    {
        return m_pImpl->hasDepth;
    }

} // namespace NS::Graphics
