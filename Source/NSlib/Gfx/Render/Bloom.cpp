#include "NSlib/Gfx/Render/Bloom.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Gfx/D3DBuffer.h"
#include "NSlib/Gfx/D3DCommandList.h"
#include "NSlib/Gfx/D3DCommonStates.h"
#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Gfx/D3DPipeline.h"
#include "NSlib/Gfx/Render/BuiltinShaders.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/D3DShader.h"
#include "NSlib/Gfx/D3DTexture.h"

#include <algorithm>
#include <utility>

namespace NS
{
    namespace Gfx
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
                float padding[2];
            };
            static_assert(sizeof(BloomCB) == 32, "BloomCB は HLSL の cbuffer b0 とバイト一致が必要");

            // 1 を超える明るさを持つ。16 ビットの浮動小数は 1 付近の刻みが 1/2048 で、8 ビットの 1/255 より細かい
            constexpr DXGI_FORMAT k_HdrFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

            std::unique_ptr<D3DTexture> MakeHdrTarget(Size2D size)
            {
                D3DTextureDesc desc{};
                desc.width = static_cast<UINT>(size.width);
                desc.height = static_cast<UINT>(size.height);
                desc.format = k_HdrFormat;
                desc.bindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
                return D3DTexture::Create(desc);
            }

            // 半分ずつ縮めた大きさ。0 にはしない
            Size2D LevelSize(Size2D size, int level)
            {
                const int shift = level + 1;
                return Size2D{std::max(1, size.width >> shift), std::max(1, size.height >> shift)};
            }
        } // namespace

        Bloom::Bloom(const BloomDesc& desc, const BuiltinShaders& shaders) noexcept
            : m_desc(desc), m_vs(shaders.fullscreenVs), m_thresholdPs(shaders.bloomThresholdPs),
              m_downPs(shaders.bloomDownPs), m_upPs(shaders.bloomUpPs), m_compositePs(shaders.bloomCompositePs)
        {
            const D3DGlobal& gpu = Gpu();
            if (gpu.device == nullptr || gpu.context == nullptr)
            {
                NS_LOG_WARN(Graphics, "Bloom: D3DDevice が未構築のため無効のまま作る");
                return;
            }

            // 全画面三角形は裏向きなので面を捨てない。世界の深度は読みも書きもしない
            D3DPipelineDesc writeDesc{};
            writeDesc.cull = CullMode::None;
            writeDesc.blend = BlendMode::Opaque;
            writeDesc.depth = DepthMode::Disabled;
            m_writePipeline = D3DPipeline::Create(writeDesc);
            D3DPipelineDesc addDesc = writeDesc;
            addDesc.blend = BlendMode::Additive;
            m_addPipeline = D3DPipeline::Create(addDesc);
            if (!m_writePipeline->IsValid() || !m_addPipeline->IsValid())
            {
                NS_LOG_ERROR(Graphics, "Bloom: パイプラインを作れなかった");
                return;
            }

            m_cb = D3DBuffer::Create(MakeConstantBufferDesc(sizeof(BloomCB)));
            if (!m_cb->IsValid())
            {
                NS_LOG_ERROR(Graphics, "Bloom: 定数バッファを作れなかった");
                return;
            }

            m_valid = true;
        }

        Bloom::~Bloom() = default;

        bool Bloom::IsValid() const noexcept
        {
            return m_valid;
        }

        void Bloom::BeginWorld(D3DDevice& device, const Color& clearColor) noexcept
        {
            if (!m_valid || m_active)
            {
                return;
            }
            if (!IsUsable(m_vs) || !IsUsable(m_thresholdPs) || !IsUsable(m_downPs) || !IsUsable(m_upPs) ||
                !IsUsable(m_compositePs))
            {
                return;
            }

            D3DCommandList& cmd = device.Commands();
            SavedTargets saved = cmd.SaveTargets();
            if (saved.color == nullptr)
            {
                return;
            }

            ComPtr<ID3D11Resource> resource;
            saved.color->GetResource(resource.GetAddressOf());
            ComPtr<ID3D11Texture2D> texture;
            if (FAILED(resource.As(&texture)))
            {
                return;
            }
            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            const Size2D size{static_cast<int>(desc.Width), static_cast<int>(desc.Height)};
            if (!EnsureTargets(size))
            {
                return;
            }

            cmd.ClearRenderTarget(m_scene->Rtv(), clearColor.R(), clearColor.G(), clearColor.B(), clearColor.A());
            cmd.SetRenderTarget(m_scene->Rtv(), saved.depth.Get());
            cmd.SetViewport(static_cast<float>(size.width), static_cast<float>(size.height));

            m_saved = std::move(saved);
            m_active = true;
        }

        void Bloom::EndWorld(D3DDevice& device) noexcept
        {
            if (!m_active)
            {
                return;
            }
            m_active = false;

            D3DCommandList& cmd = device.Commands();
            // 縮めと山形は画素の間を線形で拾う。端の外は端の色を伸ばし、画面の外から黒が入らないようにする
            cmd.PSSetSampler(device.States().LinearClamp(), 0);
            if (m_desc.intensity > 0.0f)
            {
                DrawPass(cmd, *m_thresholdPs, *m_writePipeline, *m_scene, nullptr, m_bright->Rtv(), m_size);

                // TODO: 細かい火花のにじみが明滅したら、最初の縮めで 5 つの四角を 1 / (1 + 明るさ) で重み付けする
                const D3DTexture* source = m_bright.get();
                for (std::unique_ptr<D3DTexture>& level : m_levels)
                {
                    DrawPass(cmd, *m_downPs, *m_writePipeline, *source, nullptr, level->Rtv(), level->Size());
                    source = level.get();
                }
                // 小さい段から順に 1 段大きい方へ足す。先頭の段が全部の段を持つ
                for (std::size_t i = m_levels.size() - 1; i > 0; --i)
                {
                    D3DTexture& smaller = *m_levels[i];
                    D3DTexture& larger = *m_levels[i - 1];
                    DrawPass(cmd, *m_upPs, *m_addPipeline, smaller, nullptr, larger.Rtv(), larger.Size());
                }
            }

            DrawPass(
                cmd, *m_compositePs, *m_writePipeline, *m_levels.front(), m_scene.get(), m_saved.color.Get(), m_size);
            cmd.UnbindPixelShaderResources(0, 2);

            cmd.RestoreTargets(m_saved);
            m_saved = SavedTargets{};
        }

        bool Bloom::EnsureTargets(Size2D size) noexcept
        {
            const std::size_t levelCount = static_cast<std::size_t>(std::max(1, m_desc.levels));
            if (size.width == m_size.width && size.height == m_size.height && m_levels.size() == levelCount)
            {
                return true;
            }

            m_size = Size2D{0, 0};
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

        void Bloom::DrawPass(D3DCommandList& cmd,
                             const D3DShader& pixelShader,
                             const D3DPipeline& pipeline,
                             const D3DTexture& source,
                             const D3DTexture* scene,
                             ID3D11RenderTargetView* destination,
                             Size2D destinationSize) noexcept
        {
            cmd.UnbindPixelShaderResources(0, 2);
            cmd.SetRenderTarget(destination, nullptr);
            cmd.SetViewport(static_cast<float>(destinationSize.width), static_cast<float>(destinationSize.height));

            const Size2D sourceSize = source.Size();
            BloomCB cbData{};
            cbData.sourceTexel[0] = 1.0f / static_cast<float>(sourceSize.width);
            cbData.sourceTexel[1] = 1.0f / static_cast<float>(sourceSize.height);
            cbData.destinationTexel[0] = 1.0f / static_cast<float>(destinationSize.width);
            cbData.destinationTexel[1] = 1.0f / static_cast<float>(destinationSize.height);
            cbData.threshold = m_desc.threshold;
            cbData.intensity = m_desc.intensity;
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

            cmd.DrawFromVertexId(3);
        }
    } // namespace Gfx
} // namespace NS
