#include "NSlib/Gfx/Render/ScreenPasses.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Gfx/D3DBuffer.h"
#include "NSlib/Gfx/D3DCommandList.h"
#include "NSlib/Gfx/D3DCommonStates.h"
#include "NSlib/Gfx/Render/DrawItem.h"
#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Gfx/D3DMesh.h"
#include "NSlib/Gfx/D3DPipeline.h"
#include "NSlib/Gfx/Render/BuiltinShaders.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/D3DShader.h"
#include "NSlib/Gfx/D3DTexture.h"

namespace NS
{
    namespace Gfx
    {
        ScreenPasses::ScreenPasses(const BuiltinShaders& shaders) noexcept
            : m_vs(shaders.fullscreenVs), m_distortionPs(shaders.screenDistortionPs), m_bodyMaskPs(shaders.bodyMaskPs)
        {
            const D3DGlobal& gpu = Gpu();
            if (gpu.device == nullptr || gpu.context == nullptr)
            {
                NS_LOG_WARN(Graphics, "ScreenPasses: D3DDevice が未構築のため無効のまま作る");
                return;
            }

            // 全画面三角形は裏向きなので面を捨てない
            D3DPipelineDesc writeDesc{};
            writeDesc.cull = CullMode::None;
            writeDesc.blend = BlendMode::Opaque;
            writeDesc.depth = DepthMode::Disabled;
            m_writePipeline = D3DPipeline::Create(writeDesc);
            // 世界の深度と同じ深度の画素も通すので、比べは以下
            m_bodyMaskPipelines[0] =
                D3DPipeline::Create(D3DPipelineDesc{.cull = CullMode::Back, .depth = DepthMode::ReadOnly});
            m_bodyMaskPipelines[1] =
                D3DPipeline::Create(D3DPipelineDesc{.cull = CullMode::None, .depth = DepthMode::ReadOnly});
            if (!m_writePipeline->IsValid() || !m_bodyMaskPipelines[0]->IsValid() || !m_bodyMaskPipelines[1]->IsValid())
            {
                NS_LOG_ERROR(Graphics, "ScreenPasses: パイプラインを作れなかった");
                return;
            }

            m_cb = D3DBuffer::Create(MakeConstantBufferDesc(sizeof(ScreenDistortionCB)));
            if (!m_cb->IsValid())
            {
                NS_LOG_ERROR(Graphics, "ScreenPasses: 定数バッファを作れなかった");
                return;
            }

            m_valid = true;
        }

        ScreenPasses::~ScreenPasses() = default;

        bool ScreenPasses::IsValid() const noexcept
        {
            return m_valid;
        }

        void ScreenPasses::Distort(D3DDevice& device, const ScreenRing& ring) noexcept
        {
            if (!m_valid || !(ring.pushPixels > 0.0f) || !(ring.halfWidthPixels > 0.0f))
            {
                return;
            }
            if (!IsUsable(m_vs) || !IsUsable(m_distortionPs))
            {
                return;
            }

            D3DCommandList& cmd = device.Commands();
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
            if (FAILED(resource.As(&texture)) || !EnsureCopy(*texture.Get()))
            {
                return;
            }
            D3D11_VIEWPORT savedViewport{};
            UINT viewportCount = 1;
            cmd->RSGetViewports(&viewportCount, &savedViewport);

            cmd->CopyResource(m_copy->Native(), texture.Get());

            cmd.SetRenderTarget(target.Get(), nullptr);
            cmd.SetViewport(static_cast<float>(m_copyDesc.Width), static_cast<float>(m_copyDesc.Height));

            ScreenDistortionCB cbData{};
            cbData.ring[0] = ring.centerPixel.x;
            cbData.ring[1] = ring.centerPixel.y;
            cbData.ring[2] = ring.radiusPixels;
            cbData.ring[3] = ring.pushPixels;
            cbData.ringHalfWidth = ring.halfWidthPixels;
            if (ring.keepBody)
            {
                cbData.keepBody = 1.0f;
            }
            cmd.UpdateSubresource(*m_cb, &cbData, sizeof(cbData));

            cmd.SetPipeline(*m_writePipeline);
            cmd.VSSetShader(*m_vs);
            cmd.PSSetShader(*m_distortionPs);
            cmd.PSSetConstantBuffer(*m_cb, 0);
            cmd.PSSetShaderResource(*m_copy, 0);
            const D3DTexture* bodyMask = BodyMask();
            if (bodyMask != nullptr)
            {
                cmd.PSSetShaderResource(*bodyMask, 1);
            }
            else
            {
                ID3D11ShaderResourceView* noMask[1] = {nullptr};
                cmd->PSSetShaderResources(1, 1, noMask);
            }
            // 押した先は線形で拾う。端の外は端の色を伸ばす
            cmd.PSSetSampler(device.States().LinearClamp(), 0);

            // 頂点は SV_VertexID から作る
            cmd->IASetInputLayout(nullptr);
            cmd.SetTopology(Topology::TriangleList);
            cmd.Draw(3);

            ID3D11ShaderResourceView* none[2] = {nullptr, nullptr};
            cmd->PSSetShaderResources(0, 2, none);
            cmd.SetRenderTarget(target.Get(), depth.Get());
            if (viewportCount > 0)
            {
                cmd->RSSetViewports(1, &savedViewport);
            }
        }

        bool ScreenPasses::BeginBodyMask(D3DDevice& device) noexcept
        {
            m_bodyMaskDrawn = false;
            if (!m_valid || m_savedTarget != nullptr || !IsUsable(m_bodyMaskPs))
            {
                return false;
            }

            D3DCommandList& cmd = device.Commands();
            ComPtr<ID3D11RenderTargetView> target;
            ComPtr<ID3D11DepthStencilView> depth;
            cmd->OMGetRenderTargets(1, target.GetAddressOf(), depth.GetAddressOf());
            if (target == nullptr)
            {
                return false;
            }
            ComPtr<ID3D11Resource> resource;
            target->GetResource(resource.GetAddressOf());
            ComPtr<ID3D11Texture2D> texture;
            if (FAILED(resource.As(&texture)))
            {
                return false;
            }
            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            if (!EnsureBodyMask(desc))
            {
                return false;
            }

            UINT viewportCount = 1;
            cmd->RSGetViewports(&viewportCount, &m_savedViewport);
            if (viewportCount == 0)
            {
                m_savedViewport = D3D11_VIEWPORT{
                    0.0f, 0.0f, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0.0f, 1.0f};
            }
            // 型を描画先にするので、先に読む側から外す
            ID3D11ShaderResourceView* none[2] = {nullptr, nullptr};
            cmd->PSSetShaderResources(0, 2, none);
            cmd.ClearRenderTarget(m_bodyMask->Rtv(), 0.0f, 0.0f, 0.0f, 0.0f);
            cmd.SetRenderTarget(m_bodyMask->Rtv(), depth.Get());
            cmd->RSSetViewports(1, &m_savedViewport);
            m_savedTarget = target;
            m_savedDepth = depth;
            return true;
        }

        void ScreenPasses::DrawBodyMaskItem(D3DDevice& device, const DrawItem& item) noexcept
        {
            if (m_savedTarget == nullptr)
            {
                return;
            }
            int sided = 0;
            if (item.twoSided)
            {
                sided = 1;
            }
            IssueDrawItemWith(device, item, *m_bodyMaskPipelines[sided], *m_bodyMaskPs);
        }

        void ScreenPasses::EndBodyMask(D3DDevice& device) noexcept
        {
            if (m_savedTarget == nullptr)
            {
                return;
            }
            D3DCommandList& cmd = device.Commands();
            cmd.SetRenderTarget(m_savedTarget.Get(), m_savedDepth.Get());
            cmd->RSSetViewports(1, &m_savedViewport);
            m_savedTarget.Reset();
            m_savedDepth.Reset();
            m_bodyMaskDrawn = true;
        }

        const D3DTexture* ScreenPasses::BodyMask() const noexcept
        {
            if (!m_bodyMaskDrawn)
            {
                return nullptr;
            }
            return m_bodyMask.get();
        }

        bool ScreenPasses::EnsureBodyMask(const D3D11_TEXTURE2D_DESC& target) noexcept
        {
            if (m_bodyMask != nullptr)
            {
                const Size2D size = m_bodyMask->Size();
                if (size.width == static_cast<int>(target.Width) && size.height == static_cast<int>(target.Height))
                {
                    return true;
                }
            }

            m_bodyMask.reset();
            D3DTextureDesc maskDesc{};
            maskDesc.width = target.Width;
            maskDesc.height = target.Height;
            maskDesc.format = DXGI_FORMAT_R8_UNORM;
            maskDesc.bindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            std::unique_ptr<D3DTexture> mask = D3DTexture::Create(maskDesc);
            if (!mask->IsValid())
            {
                NS_LOG_ERROR(Graphics, "ScreenPasses: {}x{} の体の型を作れなかった", target.Width, target.Height);
                return false;
            }
            m_bodyMask = std::move(mask);
            return true;
        }

        bool ScreenPasses::EnsureCopy(ID3D11Texture2D& target) noexcept
        {
            D3D11_TEXTURE2D_DESC desc{};
            target.GetDesc(&desc);
            if (m_copy != nullptr && desc.Width == m_copyDesc.Width && desc.Height == m_copyDesc.Height &&
                desc.Format == m_copyDesc.Format)
            {
                return true;
            }

            m_copy.reset();
            D3DTextureDesc copyDesc{};
            copyDesc.width = desc.Width;
            copyDesc.height = desc.Height;
            copyDesc.format = desc.Format;
            copyDesc.bindFlags = D3D11_BIND_SHADER_RESOURCE;
            std::unique_ptr<D3DTexture> copy = D3DTexture::Create(copyDesc);
            if (!copy->IsValid())
            {
                NS_LOG_ERROR(Graphics, "ScreenPasses: {}x{} の描画先の写しを作れなかった", desc.Width, desc.Height);
                return false;
            }
            m_copy = std::move(copy);
            m_copyDesc = desc;
            return true;
        }
    } // namespace Gfx
} // namespace NS
