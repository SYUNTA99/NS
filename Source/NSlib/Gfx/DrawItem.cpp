#include "NSlib/Gfx/DrawItem.h"

#include "NSlib/Gfx/D3DCommandList.h"
#include "NSlib/Gfx/D3DMaterial.h"
#include "NSlib/Gfx/D3DMesh.h"
#include "NSlib/Gfx/Renderer.h"
#include "NSlib/Gfx/D3DShader.h"

namespace NS
{
    namespace Gfx
    {
        namespace
        {
            // パイプラインは呼ぶ前に設定する
            void BindDrawItem(Renderer& renderer, const DrawItem& item) noexcept
            {
                D3DCommandList& cmd = renderer.Commands();
                item.material->CreateInputLayoutFor(*item.mesh);
                item.material->SetParams(renderer, item.constants);

                // ボーンパレットなどの追加データがあれば転送して頂点シェーダのスロットへ設定する
                if (item.extraVsCb != nullptr)
                {
                    if (item.extraVsData != nullptr && item.extraVsSize != 0)
                    {
                        cmd.UpdateSubresource(*item.extraVsCb, item.extraVsData, item.extraVsSize);
                    }
                    cmd.VSSetConstantBuffer(*item.extraVsCb, item.extraVsSlot);
                }

                item.material->Bind(renderer);
            }
        } // namespace

        void IssueDrawItem(Renderer& renderer, const DrawItem& item) noexcept
        {
            if (item.mesh == nullptr || item.material == nullptr)
            {
                return;
            }

            D3DCommandList& cmd = renderer.Commands();

            // 前回の描画ステートを引き継がないよう、描画ごとにパイプラインを設定する
            if (item.occludedOnly)
            {
                cmd.SetPipeline(renderer.OccludedPipeline(item.twoSided));
            }
            else
            {
                cmd.SetPipeline(renderer.CommonPipeline(item.blend, item.twoSided));
            }
            BindDrawItem(renderer, item);
            DrawMesh(cmd, *item.mesh);
        }

        void IssueDrawItemWith(Renderer& renderer,
                               const DrawItem& item,
                               const D3DPipeline& pipeline,
                               const D3DShader& pixelShader) noexcept
        {
            if (item.mesh == nullptr || item.material == nullptr)
            {
                return;
            }

            D3DCommandList& cmd = renderer.Commands();
            cmd.SetPipeline(pipeline);
            BindDrawItem(renderer, item);
            cmd.PSSetShader(pixelShader);
            DrawMesh(cmd, *item.mesh);
        }
    } // namespace Gfx
} // namespace NS
