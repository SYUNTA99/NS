#include "Framework/Graphics/Renderer.h"

#include "Framework/Graphics/CommonStates.h"
#include "Framework/Graphics/RenderTarget.h"
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

        std::unique_ptr<RenderTarget> mainRT;
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
        const ::NS::Core::Size2D winSize = window.Size();
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

        m_pImpl->mainRT.reset(new RenderTarget());
        if (!m_pImpl->mainRT->ConfigureAsBackbuffer(
                m_pImpl->swapchain.Get(), m_pImpl->device.Get(), m_pImpl->context.Get(), true))
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "MainRenderTarget の構築失敗");
            m_pImpl->mainRT.reset();
            return;
        }

        m_pImpl->states.reset(new CommonStates(static_cast<void*>(m_pImpl->device.Get())));

        window.SetResizeCallback([this](::NS::Core::Size2D rs) { this->Resize(rs); });

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
        if (!IsValid() || !m_pImpl->mainRT)
        {
            return;
        }
        m_pImpl->mainRT->Clear(r, g, b, a);
        m_pImpl->mainRT->Bind();
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

    void Renderer::Resize(::NS::Core::Size2D size) noexcept
    {
        if (!IsValid())
        {
            return;
        }
        if (size.width <= 0 || size.height <= 0)
        {
            return;
        }
        if (m_pImpl->mainRT)
        {
            m_pImpl->mainRT->Resize(size);
        }
    }

    ::NS::Core::Size2D Renderer::Size() const noexcept
    {
        if (!m_pImpl || !m_pImpl->mainRT)
        {
            return ::NS::Core::Size2D{0, 0};
        }
        return m_pImpl->mainRT->Size();
    }

    RenderTarget& Renderer::MainRenderTarget() noexcept
    {
        assert(m_pImpl && m_pImpl->mainRT && "Renderer が無効な状態で MainRenderTarget() を呼んでいる");
        return *m_pImpl->mainRT;
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
