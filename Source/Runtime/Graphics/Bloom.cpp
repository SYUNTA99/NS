#include "Runtime/Graphics/Bloom.h"

#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/Buffer.h"
#include "Runtime/Graphics/CommandList.h"
#include "Runtime/Graphics/GraphicObject.h"
#include "Runtime/Graphics/Mesh.h"
#include "Runtime/Graphics/Pipeline.h"
#include "Runtime/Graphics/Renderer.h"
#include "Runtime/Graphics/Shader.h"
#include "Runtime/Graphics/Texture.h"

#include <algorithm>
#include <string>

namespace NS::Gfx
{
    namespace
    {
        // シェーダの bloom.hlsli の cbuffer b0 とバイト一致させる
        struct alignas(16) BloomCB
        {
            float sourceTexel[2];
            float destinationTexel[2];
            float threshold;
            float intensity;
            float ringHalfWidth;
            float padding;
            float ring[4]; // 歪みの輪の中心の x・y、半径、押し。どれも描画先の画素
        };
        static_assert(sizeof(BloomCB) == 48, "BloomCB は HLSL の cbuffer b0 とバイト一致が必要");

        // 1 を超える明るさを持つ。16 ビットの浮動小数は 1 付近の刻みが 1/2048 で、8 ビットの 1/255 より細かい
        constexpr DXGI_FORMAT k_HdrFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

        std::unique_ptr<Texture> MakeHdrTarget(NS::Core::Size2D size)
        {
            TextureDesc desc{};
            desc.width = static_cast<UINT>(size.width);
            desc.height = static_cast<UINT>(size.height);
            desc.format = k_HdrFormat;
            desc.bindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            return Texture::Create(desc);
        }

        // 半分ずつ縮めた大きさ。0 にはしない
        NS::Core::Size2D LevelSize(NS::Core::Size2D size, int level)
        {
            const int shift = level + 1;
            return NS::Core::Size2D{std::max(1, size.width >> shift), std::max(1, size.height >> shift)};
        }

        void UnbindShaderResources(CommandList& cmd)
        {
            // 次の段で描画先にする絵が読む側に残っていると、描画装置は書き込みを捨てる
            ID3D11ShaderResourceView* none[2] = {nullptr, nullptr};
            cmd->PSSetShaderResources(0, 2, none);
        }
    } // namespace

    Bloom::Bloom(const BloomDesc& desc) noexcept : m_desc(desc)
    {
        const GraphicObject& gpu = Gpu();
        if (gpu.device == nullptr || gpu.context == nullptr)
        {
            NS_LOG_WARN(Graphics, "Bloom: Renderer が未構築のため無効のまま作る");
            return;
        }

        // 頂点バッファを使わない全画面三角形。DrawFullscreenColor と同じ物
        m_vs = Shader::CreateBuiltin("fade.vs.hlsl");
        m_thresholdPs = Shader::CreateBuiltin("bloom_threshold.ps.hlsl");
        m_downPs = Shader::CreateBuiltin("bloom_down.ps.hlsl");
        m_upPs = Shader::CreateBuiltin("bloom_up.ps.hlsl");
        m_compositePs = Shader::CreateBuiltin("bloom_composite.ps.hlsl");
        if (!m_vs->IsValid() || m_vs->IsUsingFallback() || !m_thresholdPs->IsValid() ||
            m_thresholdPs->IsUsingFallback() || !m_downPs->IsValid() || m_downPs->IsUsingFallback() ||
            !m_upPs->IsValid() || m_upPs->IsUsingFallback() || !m_compositePs->IsValid() ||
            m_compositePs->IsUsingFallback())
        {
            NS_LOG_ERROR(Graphics, "Bloom: シェーダを作れなかった");
            return;
        }

        // 全画面三角形は裏向きなので面を捨てない。世界の深度は読みも書きもしない
        PipelineDesc writeDesc{};
        writeDesc.cull = CullMode::None;
        writeDesc.blend = BlendMode::Opaque;
        writeDesc.depth = DepthMode::Disabled;
        m_writePipeline = Pipeline::Create(writeDesc);
        PipelineDesc addDesc = writeDesc;
        addDesc.blend = BlendMode::Additive;
        m_addPipeline = Pipeline::Create(addDesc);
        if (!m_writePipeline->IsValid() || !m_addPipeline->IsValid())
        {
            NS_LOG_ERROR(Graphics, "Bloom: パイプラインを作れなかった");
            return;
        }

        m_cb = Buffer::Create(MakeConstantBufferDesc(sizeof(BloomCB)));
        if (!m_cb->IsValid())
        {
            NS_LOG_ERROR(Graphics, "Bloom: 定数バッファを作れなかった");
            return;
        }

        // 縮めと山形は画素の間を線形で拾う。端の外は端の色を伸ばし、画面の外から黒が入らないようにする
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        const HRESULT hr = gpu.device->CreateSamplerState(&sampler, m_sampler.GetAddressOf());
        if (FAILED(hr))
        {
            NS_LOG_ERROR(Graphics, "Bloom: サンプラを作れなかった (hr=0x{:08X})", static_cast<unsigned>(hr));
            return;
        }

        m_valid = true;
    }

    Bloom::~Bloom() = default;

    bool Bloom::IsValid() const noexcept
    {
        return m_valid;
    }

    void Bloom::BeginWorld(Renderer& renderer, const NS::Core::Color& clearColor) noexcept
    {
        if (!m_valid || m_active)
        {
            return;
        }

        CommandList& cmd = renderer.Commands();
        ComPtr<ID3D11RenderTargetView> target;
        ComPtr<ID3D11DepthStencilView> depth;
        cmd->OMGetRenderTargets(1, target.GetAddressOf(), depth.GetAddressOf());
        if (target == nullptr)
        {
            return;
        }

        ComPtr<ID3D11Resource> resource;
        target->GetResource(resource.GetAddressOf());
        ComPtr<ID3D11Texture2D> texture;
        if (FAILED(resource.As(&texture)))
        {
            return;
        }
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        const NS::Core::Size2D size{static_cast<int>(desc.Width), static_cast<int>(desc.Height)};
        if (!EnsureTargets(size))
        {
            return;
        }

        UINT viewportCount = 1;
        cmd->RSGetViewports(&viewportCount, &m_savedViewport);
        if (viewportCount == 0)
        {
            m_savedViewport =
                D3D11_VIEWPORT{0.0f, 0.0f, static_cast<float>(size.width), static_cast<float>(size.height), 0.0f, 1.0f};
        }

        cmd.ClearRenderTarget(m_scene->Rtv(), clearColor.R(), clearColor.G(), clearColor.B(), clearColor.A());
        cmd.SetRenderTarget(m_scene->Rtv(), depth.Get());
        cmd.SetViewport(static_cast<float>(size.width), static_cast<float>(size.height));

        m_savedTarget = target;
        m_savedDepth = depth;
        m_active = true;
    }

    void Bloom::EndWorld(Renderer& renderer, const BloomRing& ring) noexcept
    {
        if (!m_active)
        {
            return;
        }
        m_active = false;
        m_ring = ring;

        CommandList& cmd = renderer.Commands();
        if (m_desc.intensity > 0.0f)
        {
            DrawPass(cmd, *m_thresholdPs, *m_writePipeline, *m_scene, nullptr, m_bright->Rtv(), m_size);

            // TODO: 細かい火花のにじみが明滅したら、最初の縮めで 5 つの四角を 1 / (1 + 明るさ) で重み付けする
            const Texture* source = m_bright.get();
            for (std::unique_ptr<Texture>& level : m_levels)
            {
                DrawPass(cmd, *m_downPs, *m_writePipeline, *source, nullptr, level->Rtv(), level->Size());
                source = level.get();
            }
            // 小さい段から順に 1 段大きい方へ足す。先頭の段が全部の段を持つ
            for (std::size_t i = m_levels.size() - 1; i > 0; --i)
            {
                Texture& smaller = *m_levels[i];
                Texture& larger = *m_levels[i - 1];
                DrawPass(cmd, *m_upPs, *m_addPipeline, smaller, nullptr, larger.Rtv(), larger.Size());
            }
        }

        DrawPass(cmd, *m_compositePs, *m_writePipeline, *m_levels.front(), m_scene.get(), m_savedTarget.Get(), m_size);
        UnbindShaderResources(cmd);

        cmd.SetRenderTarget(m_savedTarget.Get(), m_savedDepth.Get());
        cmd->RSSetViewports(1, &m_savedViewport);
        m_savedTarget.Reset();
        m_savedDepth.Reset();
    }

    bool Bloom::EnsureTargets(NS::Core::Size2D size) noexcept
    {
        const std::size_t levelCount = static_cast<std::size_t>(std::max(1, m_desc.levels));
        if (size.width == m_size.width && size.height == m_size.height && m_levels.size() == levelCount)
        {
            return true;
        }

        m_size = NS::Core::Size2D{0, 0};
        m_levels.clear();
        m_scene = MakeHdrTarget(size);
        m_bright = MakeHdrTarget(size);
        bool built = m_scene->IsValid() && m_bright->IsValid();
        for (std::size_t i = 0; i < levelCount && built; ++i)
        {
            m_levels.push_back(MakeHdrTarget(LevelSize(size, static_cast<int>(i))));
            built = m_levels.back()->IsValid();
        }
        if (!built)
        {
            NS_LOG_ERROR(Graphics, "Bloom: {}x{} の浮動小数の描画先を作れなかった", size.width, size.height);
            m_levels.clear();
            return false;
        }
        m_size = size;
        return true;
    }

    void Bloom::DrawPass(CommandList& cmd,
                         const Shader& pixelShader,
                         const Pipeline& pipeline,
                         const Texture& source,
                         const Texture* scene,
                         ID3D11RenderTargetView* destination,
                         NS::Core::Size2D destinationSize) noexcept
    {
        UnbindShaderResources(cmd);
        cmd.SetRenderTarget(destination, nullptr);
        cmd.SetViewport(static_cast<float>(destinationSize.width), static_cast<float>(destinationSize.height));

        const NS::Core::Size2D sourceSize = source.Size();
        BloomCB cbData{};
        cbData.sourceTexel[0] = 1.0f / static_cast<float>(sourceSize.width);
        cbData.sourceTexel[1] = 1.0f / static_cast<float>(sourceSize.height);
        cbData.destinationTexel[0] = 1.0f / static_cast<float>(destinationSize.width);
        cbData.destinationTexel[1] = 1.0f / static_cast<float>(destinationSize.height);
        cbData.threshold = m_desc.threshold;
        cbData.intensity = m_desc.intensity;
        cbData.ringHalfWidth = m_ring.halfWidthPixels;
        cbData.ring[0] = m_ring.centerPixel.x;
        cbData.ring[1] = m_ring.centerPixel.y;
        cbData.ring[2] = m_ring.radiusPixels;
        cbData.ring[3] = m_ring.pushPixels;
        cmd.UpdateSubresource(*m_cb, &cbData, sizeof(cbData));

        cmd.SetPipeline(pipeline);
        cmd.VSSetShader(*m_vs);
        cmd.PSSetShader(pixelShader);
        cmd.PSSetConstantBuffer(*m_cb, 0);
        cmd.PSSetShaderResource(source, 0);
        if (scene != nullptr)
        {
            cmd.PSSetShaderResource(*scene, 1);
        }
        cmd.PSSetSampler(m_sampler.Get(), 0);

        // 頂点は SV_VertexID から作るので、入力レイアウトも頂点も差さずに 3 頂点を投げる
        cmd->IASetInputLayout(nullptr);
        cmd.SetTopology(Topology::TriangleList);
        cmd.Draw(3);
    }
} // namespace NS::Gfx
