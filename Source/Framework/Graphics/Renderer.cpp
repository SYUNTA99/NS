#include "Framework/Graphics/Renderer.h"

#include "Framework/Graphics/Buffer.h"
#include "Framework/Graphics/CommandList.h"
#include "Framework/Graphics/CommonStates.h"
#include "Framework/Graphics/D3dCommon.h"
#include "Framework/Graphics/GraphicObject.h"
#include "Framework/Graphics/Pipeline.h"
#include "Framework/Graphics/Shader.h"
#include "Framework/Graphics/Texture.h"
#include "Framework/Graphics/TextureArray.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <cassert>
#include <iterator>

namespace NS::Graphics
{

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
        // 構築 / Resize の両方から呼ぶ。 いずれか失敗で false を返し out は未確定
        bool BuildBackbufferTargets(IDXGISwapChain* swapchain,
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

            std::unique_ptr<Texture> backbuffer = Texture::Create(bb, D3D11_BIND_RENDER_TARGET);
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

            std::unique_ptr<Texture> depth = Texture::Create(depthDesc);
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

    Renderer::Renderer(const RendererDesc& desc, ::NS::Platform::Window& window)
    {
        m_window = &window;
        m_vsync = desc.vsync;
        m_settings = desc.settings;

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

        bool ok = TryCreateDeviceAndSwapChain(hwnd, w, h, createFlags, m_device, m_context, m_swapchain);
        if (!ok && desc.enableDebugLayer)
        {
            NS_LOG_WARN(::NS::Core::LogCat::Graphics,
                        "Debug Layer 付きでの D3D11 デバイス作成に失敗、Debug Layer 無しで再試行");
            createFlags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
            ok = TryCreateDeviceAndSwapChain(hwnd, w, h, createFlags, m_device, m_context, m_swapchain);
        }
        if (!ok)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "D3D11 デバイス作成失敗");
            return;
        }

        // 単一 device 前提。 既に別 Renderer が公開済みならグローバルを上書きするため検知する
        if (Gpu().device != nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "Renderer を同時に複数構築している (単一 device 前提、 グローバルが上書きされる)");
        }
        // backbuffer Texture 構築より前にグローバル公開する。 構築が Gpu() を引くため
        Gpu().device = m_device.Get();
        Gpu().context = m_context.Get();

        SuppressAltEnter(m_swapchain.Get(), hwnd);

        m_commands = std::make_unique<CommandList>(m_context.Get());

        if (!BuildBackbufferTargets(m_swapchain.Get(), m_backbuffer, m_depth))
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "backbuffer / depth Texture の構築失敗");
            return;
        }

        // CommonStates のコンストラクタは friend Renderer 限定の private で make_unique が呼べないため new で構築する
        m_states.reset(new CommonStates(m_device.Get()));

        // 共通 Pipeline を1回だけ生成しキャッシュする。Gpu() は上で公開済で、描画する者が毎回 set する
        m_commonPipelines[0] = Pipeline::Create(PipelineDesc{});
        m_commonPipelines[1] = Pipeline::Create(PipelineDesc{.blend = BlendMode::Alpha, .depth = DepthMode::ReadOnly});
        m_commonPipelines[2] =
            Pipeline::Create(PipelineDesc{.blend = BlendMode::Additive, .depth = DepthMode::ReadOnly});

        window.SetResizeCallback([this](::NS::Math::Size2D rs) { this->Resize(rs); });
        m_resizeCallbackRegistered = true;

        m_valid = true;
        // 失敗時は別途 ERROR ログ済のため成功は Debug 段のみ出力し、テスト時のログ雑音を抑える
        NS_LOG_DEBUG(::NS::Core::LogCat::Graphics,
                     "Renderer 構築完了 ({}x{}, vsync={}, debugLayer={})",
                     w,
                     h,
                     desc.vsync,
                     desc.enableDebugLayer);
    }

    Renderer::~Renderer()
    {
        // コンストラクタで登録したリサイズ購読を解除し、Window 側の発火で無効参照を踏むのを防ぐ
        if (m_resizeCallbackRegistered && m_window != nullptr)
        {
            m_window->SetResizeCallback(nullptr);
        }
        // 自分が公開したグローバルだけを戻す。 別 Renderer が上書きしている場合は触らない
        if (m_device && Gpu().device == m_device.Get())
        {
            Gpu() = {};
        }
        if (m_context)
        {
            m_context->ClearState();
            m_context->Flush();
        }
        // この後 m_states / m_backbuffer / m_depth 等のメンバ破棄が宣言の逆順で走るが、
        // Gpu() は既に空のため各リソースのデストラクタから Gpu() を参照しないこと
    }

    bool Renderer::IsValid() const noexcept
    {
        return m_valid;
    }

    const Pipeline& Renderer::CommonPipeline(BlendMode blend) const noexcept
    {
        switch (blend)
        {
        case BlendMode::Alpha:
            return *m_commonPipelines[1];
        case BlendMode::Additive:
            return *m_commonPipelines[2];
        case BlendMode::Opaque:
        default:
            return *m_commonPipelines[0];
        }
    }

    void Renderer::BeginFrame(float r, float g, float b, float a) noexcept
    {
        if (!IsValid() || !m_backbuffer || !m_depth || !m_commands)
        {
            return;
        }
        CommandList& cmd = *m_commands;
        ID3D11RenderTargetView* rtv = m_backbuffer->Rtv();
        ID3D11DepthStencilView* dsv = m_depth->Dsv();

        cmd.ClearRenderTarget(rtv, r, g, b, a);
        cmd.ClearDepth(dsv, 1.0f);
        cmd.SetRenderTarget(rtv, dsv);

        const ::NS::Math::Size2D size = m_backbuffer->Size();
        cmd.SetViewport(static_cast<float>(size.width), static_cast<float>(size.height));
    }

    void Renderer::BeginFrame() noexcept
    {
        const ::NS::Math::Color& c = m_settings.clearColor;
        BeginFrame(c.R(), c.G(), c.B(), c.A());
    }

    const RenderSettings& Renderer::Settings() const noexcept
    {
        return m_settings;
    }

    void Renderer::EndFrame() noexcept
    {
        if (!IsValid() || !m_swapchain)
        {
            return;
        }

        const UINT sync = m_vsync ? 1 : 0;
        m_swapchain->Present(sync, 0);
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

        auto* context = m_context.Get();
        // ResizeBuffers の前に backbuffer 参照を全て手放す。 RTV を握ったままだと失敗する
        context->OMSetRenderTargets(0, nullptr, nullptr);
        m_backbuffer.reset();
        m_depth.reset();
        context->ClearState();
        context->Flush();

        const HRESULT hr = m_swapchain->ResizeBuffers(
            0, static_cast<UINT>(size.width), static_cast<UINT>(size.height), DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr))
        {
            NS_LOG_ERROR(
                ::NS::Core::LogCat::Graphics, "SwapChain::ResizeBuffers 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
            return;
        }

        if (!BuildBackbufferTargets(m_swapchain.Get(), m_backbuffer, m_depth))
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "Resize 後の backbuffer / depth Texture 再構築失敗");
            m_valid = false;
        }
    }

    ::NS::Math::Size2D Renderer::Size() const noexcept
    {
        if (!m_backbuffer)
        {
            return ::NS::Math::Size2D{0, 0};
        }
        return m_backbuffer->Size();
    }

    CommonStates& Renderer::States() noexcept
    {
        assert(m_states && "Renderer が無効な状態で States() を呼んでいる");
        return *m_states;
    }

    CommandList& Renderer::Commands() noexcept
    {
        assert(m_commands && "Renderer が無効な状態で Commands() を呼んでいる");
        return *m_commands;
    }

} // namespace NS::Graphics
