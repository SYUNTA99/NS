#include "NSlib/Graphics/Renderer.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/CameraData.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Graphics/D3DBuffer.h"
#include "NSlib/Graphics/D3DCommandList.h"
#include "NSlib/Graphics/D3DCommonStates.h"
#include "NSlib/Graphics/D3dCommon.h"
#include "NSlib/Graphics/GraphicObject.h"
#include "NSlib/Graphics/D3DMesh.h"
#include "NSlib/Graphics/D3DPipeline.h"
#include "NSlib/Graphics/D3DRenderTarget.h"
#include "NSlib/Graphics/D3DShader.h"
#include "NSlib/Graphics/Skybox.h"
#include "NSlib/Graphics/D3DTexture.h"
#include "NSlib/Windows/Filesystem.h"
#include "ThirdParty/DirectXTex/ScreenGrab/ScreenGrab11.h"

#include <cstring>
#include <utility>

#include <wincodec.h>

#include <iterator>
#include <memory>
#include <new>

namespace NS
{
    namespace Gfx
    {
        namespace
        {
            // 全画面塗り shader (fade.ps) の cbuffer b0 とバイト一致させる
            struct alignas(16) FullscreenColorCB
            {
                Color color;
            };
            static_assert(sizeof(FullscreenColorCB) == 16,
                          "FullscreenColorCB は HLSL の cbuffer b0 とバイト一致が必要");

            // UI 矩形 shader (ui_rect.vs / ps) の cbuffer b0 とバイト一致させる
            struct alignas(16) ScreenRectCB
            {
                float rect[4]; // clip 空間の左上の x と y、幅と高さ。高さは画面下方向の量
                Color color;
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
                                static_cast<uint>(hr),
                                static_cast<uint>(createFlags));
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

            // swapchain の backbuffer を RTV D3DTexture として包み、同サイズの depth D3DTexture を生成する
            // 構築 / Resize の両方から呼ぶ。失敗時は false を返し out を書き換えない
            bool BuildBackbufferTargets(IDXGISwapChain* swapchain,
                                        std::unique_ptr<D3DTexture>& outBackbuffer,
                                        std::unique_ptr<D3DTexture>& outDepth)
            {
                ComPtr<ID3D11Texture2D> bb;
                const HRESULT hr = swapchain->GetBuffer(0, IID_PPV_ARGS(bb.GetAddressOf()));
                if (FAILED(hr))
                {
                    NS_LOG_ERROR(Graphics, "SwapChain::GetBuffer 失敗 (hr=0x{:X})", static_cast<uint>(hr));
                    return false;
                }

                std::unique_ptr<D3DTexture> backbuffer = D3DTexture::Create(bb, D3D11_BIND_RENDER_TARGET);
                if (backbuffer->Rtv() == nullptr)
                {
                    NS_LOG_ERROR(Graphics, "backbuffer RTV の構築失敗");
                    return false;
                }

                const Size2D size = backbuffer->Size();
                D3DTextureDesc depthDesc{};
                depthDesc.width = static_cast<UINT>(size.width);
                depthDesc.height = static_cast<UINT>(size.height);
                depthDesc.format = DXGI_FORMAT_D24_UNORM_S8_UINT;
                depthDesc.bindFlags = D3D11_BIND_DEPTH_STENCIL;

                std::unique_ptr<D3DTexture> depth = D3DTexture::Create(depthDesc);
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

        Renderer::Renderer(const RendererDesc& desc, OS::Window& window) noexcept
        {
            m_window = &window;
            m_vsync = desc.vsync;
            m_settings = desc.settings;

            if (!window.IsValid())
            {
                NS_LOG_ERROR(Graphics, "Renderer 構築時に Window が無効");
                return;
            }

            HWND hwnd = window.NativeHandle();
            const Size2D winSize = window.Size();
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

            // 単一 device 前提。既に別 Renderer が公開済みならグローバルを上書きするため検知する
            if (Gpu().device != nullptr)
            {
                NS_LOG_ERROR(Graphics,
                             "Renderer を同時に複数構築している (単一 device 前提、 グローバルが上書きされる)");
            }
            // backbuffer D3DTexture 構築より前にグローバル公開する。構築が Gpu() を引くため
            Gpu().device = m_device.Get();
            Gpu().context = m_context.Get();

            SuppressAltEnter(m_swapchain.Get(), hwnd);

            // コンストラクタは friend の Renderer にだけ開いているので、make_unique では呼べない
            m_commands.reset(new D3DCommandList(m_context.Get()));

            if (!BuildBackbufferTargets(m_swapchain.Get(), m_backbuffer, m_depth))
            {
                NS_LOG_ERROR(Graphics, "backbuffer / depth D3DTexture の構築失敗");
                return;
            }

            // D3DCommonStates の private コンストラクタは make_unique から呼べない。例外を使わず nothrow new で構築し
            // 確保できたかは参照先の有無で判定する
            m_states.reset(new (std::nothrow) D3DCommonStates(m_device.Get()));
            if (!m_states)
            {
                NS_LOG_ERROR(Graphics, "D3DCommonStates の確保失敗");
                return;
            }

            // 共通 D3DPipeline を 1 回だけ生成して使い回す。D3DPipeline::Create は Gpu() を引くので公開の後に呼ぶ
            // [0] は裏を向いた面を描かない、[1] は裏も描く
            const CullMode culls[2] = {CullMode::Back, CullMode::None};
            for (int sided = 0; sided < 2; ++sided)
            {
                const CullMode cull = culls[sided];
                m_commonPipelines[sided][0] = D3DPipeline::Create(D3DPipelineDesc{.cull = cull});
                m_commonPipelines[sided][1] = D3DPipeline::Create(
                    D3DPipelineDesc{.cull = cull, .blend = BlendMode::Alpha, .depth = DepthMode::ReadOnly});
                m_commonPipelines[sided][2] = D3DPipeline::Create(
                    D3DPipelineDesc{.cull = cull, .blend = BlendMode::Additive, .depth = DepthMode::ReadOnly});
                m_occludedPipelines[sided] = D3DPipeline::Create(
                    D3DPipelineDesc{.cull = cull, .blend = BlendMode::Alpha, .depth = DepthMode::Occluded});
            }

            window.SetResizeCallback([this](Size2D rs) { this->Resize(rs); });
            m_resizeCallbackRegistered = true;

            m_valid = true;
            // 失敗時は別途 ERROR ログ済のため成功は Debug に落とす
            NS_LOG_DEBUG(Graphics,
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
            // 自分が公開したグローバルだけを戻す。別 Renderer が上書きしている場合は触らない
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

        bool Renderer::CaptureBackbufferToPng(const std::filesystem::path& path) noexcept
        {
            if (!m_valid || !m_swapchain || !m_context)
            {
                return false;
            }

            ComPtr<ID3D11Texture2D> backbuffer;
            HRESULT hr = m_swapchain->GetBuffer(0, IID_PPV_ARGS(backbuffer.GetAddressOf()));
            if (FAILED(hr))
            {
                NS_LOG_ERROR(
                    Graphics, "CaptureBackbufferToPng: backbuffer を取れない (hr=0x{:08X})", static_cast<uint>(hr));
                return false;
            }

            hr =
                DirectX::SaveWICTextureToFile(m_context.Get(), backbuffer.Get(), GUID_ContainerFormatPng, path.c_str());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(Graphics,
                             "CaptureBackbufferToPng: 書き出しに失敗 (hr=0x{:08X}): {}",
                             static_cast<uint>(hr),
                             path.string());
                return false;
            }
            return true;
        }

        bool Renderer::ReadBackbufferPixels(std::vector<uint8>& outBgra, Size2D& outSize) noexcept
        {
            if (!m_valid || !m_swapchain || !m_context)
            {
                return false;
            }

            ComPtr<ID3D11Texture2D> backbuffer;
            HRESULT hr = m_swapchain->GetBuffer(0, IID_PPV_ARGS(backbuffer.GetAddressOf()));
            if (FAILED(hr))
            {
                NS_LOG_ERROR(
                    Graphics, "ReadBackbufferPixels: backbuffer を取れない (hr=0x{:08X})", static_cast<uint>(hr));
                return false;
            }

            D3D11_TEXTURE2D_DESC desc{};
            backbuffer->GetDesc(&desc);

            const bool redAndBlueSwapped =
                desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            const bool alreadyBgra =
                desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            if (!redAndBlueSwapped && !alreadyBgra)
            {
                // R8G8B8A8 と B8G8R8A8 の UNORM 以外は BGRA に直せず、渡すと色が化けたまま気付けない
                NS_LOG_ERROR(Graphics, "ReadBackbufferPixels: BGRA へ直せない形式 ({})", static_cast<int>(desc.Format));
                return false;
            }

            // GPU 専用のままでは Map できないので、CPU から読める写しへ複写する
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0u;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0u;

            ComPtr<ID3D11Texture2D> staging;
            ID3D11Device* device = Gpu().device;
            if (device == nullptr)
            {
                return false;
            }
            hr = device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(Graphics,
                             "ReadBackbufferPixels: 読み出し用の写しを作れない (hr=0x{:08X})",
                             static_cast<uint>(hr));
                return false;
            }
            m_context->CopyResource(staging.Get(), backbuffer.Get());

            D3D11_MAPPED_SUBRESOURCE mapped{};
            hr = m_context->Map(staging.Get(), 0u, D3D11_MAP_READ, 0u, &mapped);
            if (FAILED(hr))
            {
                NS_LOG_ERROR(Graphics, "ReadBackbufferPixels: Map に失敗 (hr=0x{:08X})", static_cast<uint>(hr));
                return false;
            }

            const std::size_t rowBytes = static_cast<std::size_t>(desc.Width) * 4u;
            outBgra.resize(rowBytes * desc.Height);
            const uint8* source = static_cast<const uint8*>(mapped.pData);
            for (UINT y = 0; y < desc.Height; ++y)
            {
                std::memcpy(
                    outBgra.data() + rowBytes * y, source + static_cast<std::size_t>(mapped.RowPitch) * y, rowBytes);
            }
            m_context->Unmap(staging.Get(), 0u);

            if (redAndBlueSwapped)
            {
                // R8G8B8A8 の backbuffer は RGBA 並びなので、入れ替えないと赤と青が逆になる
                for (std::size_t at = 0; at + 2u < outBgra.size(); at += 4u)
                {
                    std::swap(outBgra[at], outBgra[at + 2u]);
                }
            }

            outSize = Size2D{static_cast<int>(desc.Width), static_cast<int>(desc.Height)};
            return true;
        }

        const D3DPipeline& Renderer::CommonPipeline(BlendMode blend, bool twoSided) const noexcept
        {
            int sided = 0;
            if (twoSided)
            {
                sided = 1;
            }
            int index = 0;
            switch (blend)
            {
            case BlendMode::Alpha:
                index = 1;
                break;
            case BlendMode::Additive:
                index = 2;
                break;
            case BlendMode::Opaque:
            default:
                index = 0;
                break;
            }
            NS_ASSERT(
                Graphics, m_commonPipelines[sided][index], "Renderer が無効な状態で CommonPipeline() を呼んでいる");
            return *m_commonPipelines[sided][index];
        }

        const D3DPipeline& Renderer::OccludedPipeline(bool twoSided) const noexcept
        {
            int sided = 0;
            if (twoSided)
            {
                sided = 1;
            }
            NS_ASSERT(Graphics, m_occludedPipelines[sided], "Renderer が無効な状態で OccludedPipeline() を呼んでいる");
            return *m_occludedPipelines[sided];
        }

        void Renderer::SetShaders(const BuiltinShaders& shaders) noexcept
        {
            m_shaders = shaders;
            // 空は作った時のシェーダーを指すので、捨てて次の DrawSky で今のシェーダーから作り直す
            m_skybox.reset();
            m_skyboxTried = false;
            m_loadedSkyboxPath.clear();
        }

        const BuiltinShaders& Renderer::Shaders() const noexcept
        {
            return m_shaders;
        }

        bool Renderer::EnsureOverlay(OverlayResources& overlay,
                                     const D3DShader* vs,
                                     const D3DShader* ps,
                                     std::size_t constantBytes,
                                     std::string_view label) noexcept
        {
            if (vs == nullptr || ps == nullptr || !vs->IsValid() || !ps->IsValid())
            {
                return false;
            }
            if (!overlay.tried)
            {
                overlay.tried = true;
                if (m_device == nullptr)
                {
                    return false;
                }

                overlay.cb = D3DBuffer::Create(MakeConstantBufferDesc(constantBytes));
                if (!overlay.cb->IsValid())
                {
                    NS_LOG_ERROR(Graphics, "Renderer: {} ConstantBuffer 構築失敗", label);
                    return false;
                }

                // 描画済みの絵の上へ半透明で重ねる。常に最前面へ出すので深度は見ない
                // 全画面三角形の 3 頂点はスクリーン空間で反時計回りになり、既定の背面カリングでは消える
                D3DPipelineDesc pipeDesc{};
                pipeDesc.cull = CullMode::None;
                pipeDesc.blend = BlendMode::Alpha;
                pipeDesc.depth = DepthMode::Disabled;
                overlay.pipeline = D3DPipeline::Create(pipeDesc);
                if (!overlay.pipeline->IsValid())
                {
                    NS_LOG_ERROR(Graphics, "Renderer: {} D3DPipeline 構築失敗", label);
                    return false;
                }

                overlay.ready = true;
            }
            return overlay.ready && m_commands && m_commands->Native() != nullptr;
        }

        void Renderer::DrawOverlay(const OverlayResources& overlay,
                                   const D3DShader& vs,
                                   const D3DShader& ps,
                                   const void* constants,
                                   std::size_t constantBytes,
                                   uint vertexCount,
                                   bool vsReadsConstants) noexcept
        {
            D3DCommandList& cmd = *m_commands;
            cmd.UpdateSubresource(*overlay.cb, constants, constantBytes);

            cmd.SetPipeline(*overlay.pipeline);
            cmd.VSSetShader(vs);
            cmd.PSSetShader(ps);
            if (vsReadsConstants)
            {
                cmd.VSSetConstantBuffer(*overlay.cb, 0);
            }
            cmd.PSSetConstantBuffer(*overlay.cb, 0);

            // VS が SV_VertexID から形を作るので、InputLayout を外し頂点もインデックスもバインドしない
            cmd->IASetInputLayout(nullptr);
            cmd.SetTopology(Topology::TriangleList);
            cmd.Draw(vertexCount);
        }

        void Renderer::DrawFullscreenColor(const Color& color) noexcept
        {
            if (!EnsureOverlay(
                    m_fullscreen, m_shaders.fullscreenVs, m_shaders.fadePs, sizeof(FullscreenColorCB), "全画面塗り"))
            {
                return;
            }

            FullscreenColorCB cbData{};
            cbData.color = color;
            DrawOverlay(m_fullscreen, *m_shaders.fullscreenVs, *m_shaders.fadePs, &cbData, sizeof(cbData), 3, false);
        }

        void Renderer::DrawScreenRect(float x, float y, float width, float height, const Color& color) noexcept
        {
            if (!EnsureOverlay(
                    m_screenRect, m_shaders.screenRectVs, m_shaders.screenRectPs, sizeof(ScreenRectCB), "UI 矩形"))
            {
                return;
            }

            const Size2D targetSize = Size();
            if (targetSize.width <= 0 || targetSize.height <= 0)
            {
                NS_LOG_WARN(
                    Graphics, "Renderer: DrawScreenRect 無効な描画先サイズ {}x{}", targetSize.width, targetSize.height);
                return;
            }
            const float targetWidth = static_cast<float>(targetSize.width);
            const float targetHeight = static_cast<float>(targetSize.height);

            // ピクセル (左上原点) → clip 空間 (中央原点・y 上向き) へ CPU 側で変換して渡す
            ScreenRectCB cbData{};
            cbData.rect[0] = x / targetWidth * 2.0f - 1.0f;
            cbData.rect[1] = 1.0f - y / targetHeight * 2.0f;
            cbData.rect[2] = width / targetWidth * 2.0f;
            cbData.rect[3] = height / targetHeight * 2.0f;
            cbData.color = color;
            DrawOverlay(m_screenRect, *m_shaders.screenRectVs, *m_shaders.screenRectPs, &cbData, sizeof(cbData), 6, true);
        }

        void Renderer::EnsureSkyboxResources() noexcept
        {
            if (m_skyboxTried)
            {
                return;
            }
            m_skyboxTried = true;

            if (m_device == nullptr)
            {
                NS_LOG_WARN(Graphics, "Renderer: device が無いため空を描かない");
                return;
            }

            std::unique_ptr<Skybox> skybox = Skybox::Create(m_shaders);
            if (!skybox || !skybox->IsValid())
            {
                NS_LOG_WARN(Graphics, "Renderer: Skybox の構築失敗のため空を描かない");
                return;
            }
            m_skybox = std::move(skybox);
        }

        void Renderer::DrawSky(const CameraData& camera, std::string_view cubemapPath) noexcept
        {
            // cubemap を指定していないシーンは空を持たない。Skybox の構築もしない
            if (cubemapPath.empty())
            {
                return;
            }

            EnsureSkyboxResources();
            if (!m_skybox || !m_commands || !m_states)
            {
                return;
            }

            // 毎フレーム LoadCubemap するとファイル読み込みが常時走る
            if (cubemapPath != m_loadedSkyboxPath)
            {
                // ユーザー編集ファイル由来のパスを ContentRoot 配下へ閉じ込める。外を指す値は読み込まない
                const std::optional<std::string> absPath =
                    OS::FileSystem::ResolveUnder(OS::FileSystem::ContentRoot(), cubemapPath);
                if (!absPath.has_value())
                {
                    NS_LOG_WARN(
                        Graphics, "Renderer: cubemap パス '{}' は ContentRoot 配下でないため読み込まない", cubemapPath);
                    // 拒否はパスを直すまで変わらないので、覚えて警告の連打を止める
                    m_loadedSkyboxPath = cubemapPath;
                }
                else if (m_skybox->LoadCubemap(*absPath))
                {
                    m_loadedSkyboxPath = cubemapPath;
                }
                else
                {
                    NS_LOG_WARN(Graphics, "Renderer: cubemap 読込失敗 ({}), 既存を維持", *absPath);
                    // 失敗時は前回パスを更新しないので次フレームで再試行できる
                }
            }

            // view の平行移動成分を 0 化して camera 中心に空を固定する
            Matrix viewNoTranslate = camera.View();
            viewNoTranslate._41 = 0.0f;
            viewNoTranslate._42 = 0.0f;
            viewNoTranslate._43 = 0.0f;
            m_skybox->Draw(*m_commands, viewNoTranslate * camera.Projection(), m_states->LinearClamp());
        }

        void Renderer::BeginFrame(float r, float g, float b, float a) noexcept
        {
            if (!IsValid() || !m_backbuffer || !m_depth || !m_commands)
            {
                return;
            }
            D3DCommandList& cmd = *m_commands;
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
                BindTarget(sceneRtv, sceneDsv, m_sceneTarget->Size());
                return;
            }

            BindTarget(rtv, dsv, m_backbuffer->Size());
        }

        void Renderer::BeginFrame() noexcept
        {
            const Color& c = m_settings.clearColor;
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
                NS_LOG_ERROR(Graphics, "SwapChain::Present 失敗 (hr=0x{:X})", static_cast<uint>(hr));
                // device 喪失は復帰不能。以降の描画を止め、毎フレームのログ連発も防ぐ
                if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
                {
                    m_valid = false;
                }
            }
        }

        void Renderer::Resize(Size2D size) noexcept
        {
            if (!IsValid())
            {
                return;
            }
            if (size.width <= 0 || size.height <= 0)
            {
                return;
            }

            ID3D11DeviceContext* context = m_context.Get();
            // ResizeBuffers の前に backbuffer 参照を全て手放す。RTV を握ったままだと失敗する
            context->OMSetRenderTargets(0, nullptr, nullptr);
            m_backbuffer.reset();
            m_depth.reset();
            context->ClearState();
            context->Flush();

            // 第 1 引数の 0 と DXGI_FORMAT_UNKNOWN は今のバッファ数とフォーマットを保つ指定
            const HRESULT hr = m_swapchain->ResizeBuffers(
                0, static_cast<UINT>(size.width), static_cast<UINT>(size.height), DXGI_FORMAT_UNKNOWN, 0);
            if (FAILED(hr))
            {
                NS_LOG_ERROR(Graphics, "SwapChain::ResizeBuffers 失敗 (hr=0x{:X})", static_cast<uint>(hr));
                return;
            }

            if (!BuildBackbufferTargets(m_swapchain.Get(), m_backbuffer, m_depth))
            {
                NS_LOG_ERROR(Graphics, "Resize 後の backbuffer / depth D3DTexture 再構築失敗");
                m_valid = false;
            }
        }

        void Renderer::SetSceneTarget(D3DRenderTarget* target) noexcept
        {
            m_sceneTarget = target;
        }

        void Renderer::BeginSceneView(D3DRenderTarget* target) noexcept
        {
            if (!IsValid() || !m_commands)
            {
                return;
            }
            // Size() が今のビューを返すよう描画先を差し替える。RenderWorld のアスペクト比計算がこれを読む
            m_sceneTarget = target;
            if (target != nullptr && target->IsValid())
            {
                ID3D11RenderTargetView* rtv = target->Color()->Rtv();
                ID3D11DepthStencilView* dsv = target->Depth()->Dsv();
                const Color& c = m_settings.clearColor;
                m_commands->ClearRenderTarget(rtv, c.R(), c.G(), c.B(), c.A());
                m_commands->ClearDepth(dsv, 1.0f);
                BindTarget(rtv, dsv, target->Size());
                return;
            }
            BindBackbuffer();
        }

        void Renderer::BindBackbuffer() noexcept
        {
            if (!IsValid() || !m_backbuffer || !m_depth || !m_commands)
            {
                return;
            }
            BindTarget(m_backbuffer->Rtv(), m_depth->Dsv(), m_backbuffer->Size());
        }

        void Renderer::BindTarget(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, Size2D size) noexcept
        {
            m_commands->SetRenderTarget(rtv, dsv);
            m_commands->SetViewport(static_cast<float>(size.width), static_cast<float>(size.height));
        }

        Size2D Renderer::Size() const noexcept
        {
            if (m_sceneTarget != nullptr && m_sceneTarget->IsValid())
            {
                return m_sceneTarget->Size();
            }
            if (!m_backbuffer)
            {
                return Size2D{0, 0};
            }
            return m_backbuffer->Size();
        }

        D3DCommonStates& Renderer::States() noexcept
        {
            NS_ASSERT(Graphics, m_states, "Renderer が無効な状態で States() を呼んでいる");
            return *m_states;
        }

        D3DCommandList& Renderer::Commands() noexcept
        {
            NS_ASSERT(Graphics, m_commands, "Renderer が無効な状態で Commands() を呼んでいる");
            return *m_commands;
        }
    } // namespace Gfx
} // namespace NS
