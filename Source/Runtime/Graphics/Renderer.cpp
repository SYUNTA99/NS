#include "Runtime/Graphics/Renderer.h"

#include "Runtime/Core/Filesystem.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/Buffer.h"
#include "Runtime/Graphics/Camera.h"
#include "Runtime/Graphics/CommandList.h"
#include "Runtime/Graphics/CommonStates.h"
#include "Runtime/Graphics/D3dCommon.h"
#include "Runtime/Graphics/GraphicObject.h"
#include "Runtime/Graphics/Mesh.h"
#include "Runtime/Graphics/Pipeline.h"
#include "Runtime/Graphics/RenderTarget.h"
#include "Runtime/Graphics/Shader.h"
#include "Runtime/Graphics/Skybox.h"
#include "Runtime/Graphics/Texture.h"

#include <cassert>
#include <iterator>
#include <memory>
#include <new>

namespace NS::Graphics
{

    namespace
    {
        // 全画面塗り shader (fade.ps) の cbuffer b0 とバイト一致させる
        struct alignas(16) FullscreenColorCB
        {
            NS::Core::Color color;
        };
        static_assert(sizeof(FullscreenColorCB) == 16, "FullscreenColorCB は HLSL の cbuffer b0 とバイト一致が必要");

        // UI 矩形 shader (ui_rect.vs / ps) の cbuffer b0 とバイト一致させる
        struct alignas(16) ScreenRectCB
        {
            float rect[4]; // clip 空間の左上 x, y と幅, 高さ (高さは画面下方向の量)
            NS::Core::Color color;
        };
        static_assert(sizeof(ScreenRectCB) == 32, "ScreenRectCB は HLSL の cbuffer b0 とバイト一致が必要");

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
                NS_LOG_WARN(Graphics,
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
                NS_LOG_ERROR(Graphics, "SwapChain::GetBuffer 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
                return false;
            }

            std::unique_ptr<Texture> backbuffer = Texture::Create(bb, D3D11_BIND_RENDER_TARGET);
            if (backbuffer->Rtv() == nullptr)
            {
                NS_LOG_ERROR(Graphics, "backbuffer RTV の構築失敗");
                return false;
            }

            const ::NS::Core::Size2D size = backbuffer->Size();
            TextureCreateDesc depthDesc{};
            depthDesc.width = static_cast<UINT>(size.width);
            depthDesc.height = static_cast<UINT>(size.height);
            depthDesc.format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            depthDesc.bindFlags = D3D11_BIND_DEPTH_STENCIL;

            std::unique_ptr<Texture> depth = Texture::Create(depthDesc);
            if (depth->Dsv() == nullptr)
            {
                NS_LOG_ERROR(Graphics, "depth DSV の構築失敗");
                return false;
            }

            outBackbuffer = std::move(backbuffer);
            outDepth = std::move(depth);
            return true;
        }
    } // namespace

    Renderer::Renderer(const RendererDesc& desc, ::NS::Platform::Window& window) noexcept
    {
        m_window = &window;
        m_vsync = desc.vsync;
        m_settings = desc.settings;

        if (!window.IsValid())
        {
            NS_LOG_ERROR(Graphics, "Renderer 構築時に Window が無効");
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

        bool ok = TryCreateDeviceAndSwapChain(hwnd, w, h, createFlags, m_device, m_context, m_swapchain);
        if (!ok && desc.enableDebugLayer)
        {
            NS_LOG_WARN(Graphics, "Debug Layer 付きでの D3D11 デバイス作成に失敗、Debug Layer 無しで再試行");
            createFlags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
            ok = TryCreateDeviceAndSwapChain(hwnd, w, h, createFlags, m_device, m_context, m_swapchain);
        }
        if (!ok)
        {
            NS_LOG_ERROR(Graphics, "D3D11 デバイス作成失敗");
            return;
        }

        // 単一 device 前提。 既に別 Renderer が公開済みならグローバルを上書きするため検知する
        if (Gpu().device != nullptr)
        {
            NS_LOG_ERROR(Graphics, "Renderer を同時に複数構築している (単一 device 前提、 グローバルが上書きされる)");
        }
        // backbuffer Texture 構築より前にグローバル公開する。 構築が Gpu() を引くため
        Gpu().device = m_device.Get();
        Gpu().context = m_context.Get();

        SuppressAltEnter(m_swapchain.Get(), hwnd);

        m_commands = std::make_unique<CommandList>(m_context.Get());

        if (!BuildBackbufferTargets(m_swapchain.Get(), m_backbuffer, m_depth))
        {
            NS_LOG_ERROR(Graphics, "backbuffer / depth Texture の構築失敗");
            return;
        }

        // CommonStates の private コンストラクタは make_unique から呼べない。 例外を使わず nothrow new で構築し
        // 確保失敗は null 判定で扱う
        m_states.reset(new (std::nothrow) CommonStates(m_device.Get()));
        if (!m_states)
        {
            NS_LOG_ERROR(Graphics, "CommonStates の確保失敗");
            return;
        }

        // 共通 Pipeline を1回だけ生成しキャッシュする。Gpu() は上で公開済で、描画する者が毎回 set する
        m_commonPipelines[0] = Pipeline::Create(PipelineDesc{});
        m_commonPipelines[1] = Pipeline::Create(PipelineDesc{.blend = BlendMode::Alpha, .depth = DepthMode::ReadOnly});
        m_commonPipelines[2] =
            Pipeline::Create(PipelineDesc{.blend = BlendMode::Additive, .depth = DepthMode::ReadOnly});

        window.SetResizeCallback([this](::NS::Core::Size2D rs) { this->Resize(rs); });
        m_resizeCallbackRegistered = true;

        m_valid = true;
        // 失敗時は別途 ERROR ログ済のため成功は Debug 段のみ出力し、テスト時のログ雑音を抑える
        NS_LOG_DEBUG(
            Graphics, "Renderer 構築完了 ({}x{}, vsync={}, debugLayer={})", w, h, desc.vsync, desc.enableDebugLayer);
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

    void Renderer::EnsureFullscreenResources() noexcept
    {
        if (m_fullscreenTried)
            return;
        m_fullscreenTried = true;

        if (m_device == nullptr)
            return;

        const auto contentRoot = ::NS::Core::FileSystem::ContentRoot();
        m_fullscreenVs = Shader::Create(contentRoot / "Shaders" / "fade.vs.hlsl");
        m_fullscreenPs = Shader::Create(contentRoot / "Shaders" / "fade.ps.hlsl");
        if (!m_fullscreenVs->IsValid() || !m_fullscreenPs->IsValid())
        {
            NS_LOG_ERROR(Graphics, "Renderer: 全画面塗り shader 構築失敗");
            return;
        }

        BufferDesc cbDesc = MakeConstantBufferDesc(sizeof(FullscreenColorCB));
        m_fullscreenCb = Buffer::Create(cbDesc);
        if (!m_fullscreenCb->IsValid())
        {
            NS_LOG_ERROR(Graphics, "Renderer: 全画面塗り ConstantBuffer 構築失敗");
            return;
        }

        // 描画済みシーンの上へ半透明で重ねる。深度は無効で常に最前面、全画面三角形なのでカリングも無効
        PipelineDesc pipeDesc{};
        pipeDesc.cull = CullMode::None;
        pipeDesc.blend = BlendMode::Alpha;
        pipeDesc.depth = DepthMode::Disabled;
        m_fullscreenPipeline = Pipeline::Create(pipeDesc);
        if (!m_fullscreenPipeline->IsValid())
        {
            NS_LOG_ERROR(Graphics, "Renderer: 全画面塗り Pipeline 構築失敗");
            return;
        }

        m_fullscreenReady = true;
    }

    void Renderer::DrawFullscreenColor(const NS::Core::Color& color) noexcept
    {
        EnsureFullscreenResources();
        if (!m_fullscreenReady || !m_commands)
            return;

        CommandList& cmd = *m_commands;
        if (cmd.Native() == nullptr)
            return;

        FullscreenColorCB cbData{};
        cbData.color = color;
        cmd.UpdateSubresource(*m_fullscreenCb, &cbData, sizeof(cbData));

        cmd.SetPipeline(*m_fullscreenPipeline);
        cmd.VSSetShader(*m_fullscreenVs);
        cmd.PSSetShader(*m_fullscreenPs);
        cmd.PSSetConstantBuffer(*m_fullscreenCb, 0);

        // VS が SV_VertexID から三角形を作るので、InputLayout を外し頂点もインデックスもバインドせず 3 頂点を投げる
        cmd->IASetInputLayout(nullptr);
        cmd.SetTopology(Topology::TriangleList);
        cmd.Draw(3);
    }

    void Renderer::EnsureScreenRectResources() noexcept
    {
        if (m_screenRectTried)
            return;
        m_screenRectTried = true;

        if (m_device == nullptr)
            return;

        const auto contentRoot = ::NS::Core::FileSystem::ContentRoot();
        m_screenRectVs = Shader::Create(contentRoot / "Shaders" / "ui_rect.vs.hlsl");
        m_screenRectPs = Shader::Create(contentRoot / "Shaders" / "ui_rect.ps.hlsl");
        if (!m_screenRectVs->IsValid() || !m_screenRectPs->IsValid())
        {
            NS_LOG_ERROR(Graphics, "Renderer: UI 矩形 shader 構築失敗");
            return;
        }

        BufferDesc cbDesc = MakeConstantBufferDesc(sizeof(ScreenRectCB));
        m_screenRectCb = Buffer::Create(cbDesc);
        if (!m_screenRectCb->IsValid())
        {
            NS_LOG_ERROR(Graphics, "Renderer: UI 矩形 ConstantBuffer 構築失敗");
            return;
        }

        // 描画済みの絵の上へ半透明で重ねる。深度は無効で常に最前面、画面向き矩形なのでカリングも無効
        PipelineDesc pipeDesc{};
        pipeDesc.cull = CullMode::None;
        pipeDesc.blend = BlendMode::Alpha;
        pipeDesc.depth = DepthMode::Disabled;
        m_screenRectPipeline = Pipeline::Create(pipeDesc);
        if (!m_screenRectPipeline->IsValid())
        {
            NS_LOG_ERROR(Graphics, "Renderer: UI 矩形 Pipeline 構築失敗");
            return;
        }

        m_screenRectReady = true;
    }

    void Renderer::DrawScreenRect(float x, float y, float width, float height, const NS::Core::Color& color) noexcept
    {
        EnsureScreenRectResources();
        if (!m_screenRectReady || !m_commands)
            return;

        CommandList& cmd = *m_commands;
        if (cmd.Native() == nullptr)
            return;

        const ::NS::Core::Size2D targetSize = Size();
        if (targetSize.width <= 0 || targetSize.height <= 0)
            return;
        const float targetWidth = static_cast<float>(targetSize.width);
        const float targetHeight = static_cast<float>(targetSize.height);

        // ピクセル (左上原点) → clip 空間 (中央原点・y 上向き) へ CPU 側で変換して渡す
        ScreenRectCB cbData{};
        cbData.rect[0] = x / targetWidth * 2.0f - 1.0f;
        cbData.rect[1] = 1.0f - y / targetHeight * 2.0f;
        cbData.rect[2] = width / targetWidth * 2.0f;
        cbData.rect[3] = height / targetHeight * 2.0f;
        cbData.color = color;
        cmd.UpdateSubresource(*m_screenRectCb, &cbData, sizeof(cbData));

        cmd.SetPipeline(*m_screenRectPipeline);
        cmd.VSSetShader(*m_screenRectVs);
        cmd.PSSetShader(*m_screenRectPs);
        cmd.VSSetConstantBuffer(*m_screenRectCb, 0);
        cmd.PSSetConstantBuffer(*m_screenRectCb, 0);

        // VS が SV_VertexID から矩形を作るので、InputLayout を外し頂点もインデックスもバインドせず 6 頂点を投げる
        cmd->IASetInputLayout(nullptr);
        cmd.SetTopology(Topology::TriangleList);
        cmd.Draw(6);
    }

    void Renderer::EnsureSkyboxResources() noexcept
    {
        if (m_skyboxTried)
            return;
        m_skyboxTried = true;

        if (m_device == nullptr)
            return;

        auto skybox = Skybox::Create();
        if (!skybox || !skybox->IsValid())
        {
            NS_LOG_WARN(Graphics, "Renderer: skybox 装置の構築失敗のため空を描かない");
            return;
        }
        m_skybox = std::move(skybox);
    }

    void Renderer::DrawSky(const Camera& camera, const std::filesystem::path& cubemapPath) noexcept
    {
        // cubemap を指定していないシーンは空を持たない。装置の構築もしない
        if (cubemapPath.empty())
            return;

        EnsureSkyboxResources();
        if (!m_skybox)
            return;

        // 毎フレーム LoadCubemap すると I/O が常時走るため、前回パスと差分があるときだけ再ロードする
        if (cubemapPath != m_loadedSkyboxPath)
        {
            // ユーザー編集ファイル由来のパスを ContentRoot 配下へ閉じ込める。外を指す値は読み込まない
            const auto absPath =
                ::NS::Core::FileSystem::ResolveUnder(::NS::Core::FileSystem::ContentRoot(), cubemapPath);
            if (!absPath.has_value())
            {
                NS_LOG_WARN(Graphics,
                            "Renderer: cubemap パス '{}' は ContentRoot 配下でないため読み込まない",
                            cubemapPath.string());
                // 拒否はパスを直すまで変わらないので、覚えて警告の連打を止める
                m_loadedSkyboxPath = cubemapPath;
            }
            else if (m_skybox->LoadCubemap(*absPath))
            {
                m_loadedSkyboxPath = cubemapPath;
            }
            else
            {
                NS_LOG_WARN(Graphics, "Renderer: cubemap 読込失敗 ({}), 既存を維持", absPath->string());
                // 失敗時は前回パスを更新しないので次フレームで再試行できる
            }
        }

        // view の平行移動成分を 0 化して camera 中心に空を固定する
        NS::Core::Matrix viewNoTranslate = camera.View();
        viewNoTranslate._41 = 0.0f;
        viewNoTranslate._42 = 0.0f;
        viewNoTranslate._43 = 0.0f;
        IssueSkybox(*this, *m_skybox, viewNoTranslate * camera.Projection());
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

        // オフスクリーン設定中はそちらを描画先にする。backbuffer は後段の UI 描画用に先へクリア済
        if (m_sceneTarget != nullptr && m_sceneTarget->IsValid())
        {
            ID3D11RenderTargetView* sceneRtv = m_sceneTarget->Color()->Rtv();
            ID3D11DepthStencilView* sceneDsv = m_sceneTarget->Depth()->Dsv();
            cmd.ClearRenderTarget(sceneRtv, r, g, b, a);
            cmd.ClearDepth(sceneDsv, 1.0f);
            cmd.SetRenderTarget(sceneRtv, sceneDsv);
            const ::NS::Core::Size2D sceneSize = m_sceneTarget->Size();
            cmd.SetViewport(static_cast<float>(sceneSize.width), static_cast<float>(sceneSize.height));
            return;
        }

        cmd.SetRenderTarget(rtv, dsv);
        const ::NS::Core::Size2D size = m_backbuffer->Size();
        cmd.SetViewport(static_cast<float>(size.width), static_cast<float>(size.height));
    }

    void Renderer::BeginFrame() noexcept
    {
        const ::NS::Core::Color& c = m_settings.clearColor;
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

        const UINT sync = [&]() -> UINT {
            if (m_vsync)
            {
                return 1;
            }
            return 0;
        }();
        const HRESULT hr = m_swapchain->Present(sync, 0);
        if (FAILED(hr))
        {
            NS_LOG_ERROR(Graphics, "SwapChain::Present 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
            // device 喪失は復帰不能。 以降の描画を止め、 毎フレームのログ連発も防ぐ
            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            {
                m_valid = false;
            }
        }
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
            NS_LOG_ERROR(Graphics, "SwapChain::ResizeBuffers 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
            return;
        }

        if (!BuildBackbufferTargets(m_swapchain.Get(), m_backbuffer, m_depth))
        {
            NS_LOG_ERROR(Graphics, "Resize 後の backbuffer / depth Texture 再構築失敗");
            m_valid = false;
        }
    }

    void Renderer::SetSceneTarget(RenderTarget* target) noexcept
    {
        m_sceneTarget = target;
    }

    void Renderer::BeginSceneView(RenderTarget* target) noexcept
    {
        if (!IsValid() || !m_commands)
        {
            return;
        }
        // Size() が今のビューを返すよう描画先を差し替える。RenderWorld のアスペクト比計算がこれを読む
        m_sceneTarget = target;
        CommandList& cmd = *m_commands;
        const ::NS::Core::Color& c = m_settings.clearColor;
        if (target != nullptr && target->IsValid())
        {
            ID3D11RenderTargetView* rtv = target->Color()->Rtv();
            ID3D11DepthStencilView* dsv = target->Depth()->Dsv();
            cmd.ClearRenderTarget(rtv, c.R(), c.G(), c.B(), c.A());
            cmd.ClearDepth(dsv, 1.0f);
            cmd.SetRenderTarget(rtv, dsv);
            const ::NS::Core::Size2D size = target->Size();
            cmd.SetViewport(static_cast<float>(size.width), static_cast<float>(size.height));
            return;
        }
        if (!m_backbuffer || !m_depth)
        {
            return;
        }
        cmd.SetRenderTarget(m_backbuffer->Rtv(), m_depth->Dsv());
        const ::NS::Core::Size2D size = m_backbuffer->Size();
        cmd.SetViewport(static_cast<float>(size.width), static_cast<float>(size.height));
    }

    void Renderer::BindBackbuffer() noexcept
    {
        if (!IsValid() || !m_backbuffer || !m_depth || !m_commands)
        {
            return;
        }
        CommandList& cmd = *m_commands;
        cmd.SetRenderTarget(m_backbuffer->Rtv(), m_depth->Dsv());
        const ::NS::Core::Size2D size = m_backbuffer->Size();
        cmd.SetViewport(static_cast<float>(size.width), static_cast<float>(size.height));
    }

    ::NS::Core::Size2D Renderer::Size() const noexcept
    {
        if (m_sceneTarget != nullptr && m_sceneTarget->IsValid())
        {
            return m_sceneTarget->Size();
        }
        if (!m_backbuffer)
        {
            return ::NS::Core::Size2D{0, 0};
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
