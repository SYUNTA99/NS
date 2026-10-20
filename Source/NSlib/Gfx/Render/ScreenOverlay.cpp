#include "NSlib/Gfx/Render/ScreenOverlay.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Gfx/D3DBuffer.h"
#include "NSlib/Gfx/D3DCommandList.h"
#include "NSlib/Gfx/D3DMesh.h"
#include "NSlib/Gfx/D3DPipeline.h"
#include "NSlib/Gfx/D3DShader.h"
#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Gfx/Render/BuiltinShaders.h"
#include "NSlib/Gfx/D3DDevice.h"

namespace NS
{
    namespace Gfx
    {
        ScreenOverlay::ScreenOverlay(D3DDevice* device, const BuiltinShaders& shaders) noexcept
            : m_device(device), m_fullscreenVs(shaders.fullscreenVs), m_fadePs(shaders.fadePs),
              m_screenRectVs(shaders.screenRectVs), m_screenRectPs(shaders.screenRectPs)
        {
            NS_ASSERT(Graphics, device != nullptr, "null の device で ScreenOverlay を作っている");
        }

        ScreenOverlay::~ScreenOverlay() = default;

        bool ScreenOverlay::EnsurePass(Pass& pass,
                                       const D3DShader* vs,
                                       const D3DShader* ps,
                                       std::size_t constantBytes,
                                       std::string_view label) noexcept
        {
            if (!CanDrawFromVertexId(vs) || !IsUsable(ps))
            {
                return false;
            }
            if (!pass.tried)
            {
                pass.tried = true;
                if (Gpu().device == nullptr)
                {
                    return false;
                }

                pass.cb = D3DBuffer::Create(MakeConstantBufferDesc(constantBytes));
                if (!pass.cb->IsValid())
                {
                    NS_LOG_ERROR(Graphics, "ScreenOverlay: {} ConstantBuffer 構築失敗", label);
                    return false;
                }

                // 描画済みの絵の上へ半透明で重ねる。常に最前面へ出すので深度は見ない
                // 全画面三角形の 3 頂点はスクリーン空間で反時計回りになり、既定の背面カリングでは消える
                D3DPipelineDesc pipeDesc{};
                pipeDesc.cull = CullMode::None;
                pipeDesc.blend = BlendMode::Alpha;
                pipeDesc.depth = DepthMode::Disabled;
                pass.pipeline = D3DPipeline::Create(pipeDesc);
                if (!pass.pipeline->IsValid())
                {
                    NS_LOG_ERROR(Graphics, "ScreenOverlay: {} D3DPipeline 構築失敗", label);
                    return false;
                }

                pass.ready = true;
            }
            return pass.ready && m_device->IsValid() && m_device->Commands().Native() != nullptr;
        }

        void ScreenOverlay::DrawPass(const Pass& pass,
                                     const D3DShader& vs,
                                     const D3DShader& ps,
                                     const void* constants,
                                     std::size_t constantBytes,
                                     uint vertexCount,
                                     bool vsReadsConstants) noexcept
        {
            D3DCommandList& cmd = m_device->Commands();
            cmd.UpdateSubresource(*pass.cb, constants, constantBytes);

            cmd.SetPipeline(*pass.pipeline);
            cmd.VSSetShader(vs);
            cmd.PSSetShader(ps);
            if (vsReadsConstants)
            {
                cmd.VSSetConstantBuffer(*pass.cb, k_DrawConstantsSlot);
            }
            cmd.PSSetConstantBuffer(*pass.cb, k_DrawConstantsSlot);

            // 頂点もインデックスも差さない
            cmd.DrawFromVertexId(vertexCount);
        }

        void ScreenOverlay::DrawFullscreenColor(const Color& color) noexcept
        {
            if (!EnsurePass(m_fullscreen, m_fullscreenVs, m_fadePs, sizeof(FullscreenColorCB), "全画面塗り"))
            {
                return;
            }

            FullscreenColorCB cbData{};
            cbData.color = color;
            DrawPass(m_fullscreen, *m_fullscreenVs, *m_fadePs, &cbData, sizeof(cbData), 3, false);
        }

        void ScreenOverlay::DrawScreenRect(float x, float y, float width, float height, const Color& color) noexcept
        {
            if (!EnsurePass(m_screenRect, m_screenRectVs, m_screenRectPs, sizeof(ScreenRectCB), "UI 矩形"))
            {
                return;
            }

            const Size2D targetSize = m_device->Size();
            if (targetSize.width <= 0 || targetSize.height <= 0)
            {
                NS_LOG_WARN(Graphics,
                            "ScreenOverlay: DrawScreenRect 無効な描画先サイズ {}x{}",
                            targetSize.width,
                            targetSize.height);
                return;
            }
            const float targetWidth = static_cast<float>(targetSize.width);
            const float targetHeight = static_cast<float>(targetSize.height);

            ScreenRectCB cbData{};
            cbData.rect[0] = x / targetWidth * 2.0f - 1.0f;
            cbData.rect[1] = 1.0f - y / targetHeight * 2.0f;
            cbData.rect[2] = width / targetWidth * 2.0f;
            cbData.rect[3] = height / targetHeight * 2.0f;
            cbData.color = color;
            DrawPass(m_screenRect, *m_screenRectVs, *m_screenRectPs, &cbData, sizeof(cbData), 6, true);
        }
    } // namespace Gfx
} // namespace NS
