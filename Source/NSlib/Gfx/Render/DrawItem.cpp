#include "NSlib/Gfx/Render/DrawItem.h"

#include "NSlib/Gfx/D3DCommandList.h"
#include "NSlib/Gfx/D3DMaterial.h"
#include "NSlib/Gfx/D3DMesh.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/D3DShader.h"

namespace NS
{
    namespace Gfx
    {
        namespace
        {
            // パイプラインは呼ぶ前に設定する
            void BindDrawItem(D3DDevice& device, const DrawItem& item) noexcept
            {
                D3DCommandList& cmd = device.Commands();
                item.material->CreateInputLayoutFor(*item.mesh);
                item.material->SetParams(device, item.constants);

                // ボーンパレットなどの追加データがあれば転送して頂点シェーダのスロットへ設定する
                if (item.extraVsCb != nullptr)
                {
                    if (item.extraVsData != nullptr && item.extraVsSize != 0)
                    {
                        cmd.UpdateSubresource(*item.extraVsCb, item.extraVsData, item.extraVsSize);
                    }
                    cmd.VSSetConstantBuffer(*item.extraVsCb, item.extraVsSlot);
                }

                item.material->Bind(device);
            }
        } // namespace

        void IssueDrawItem(D3DDevice& device, const DrawItem& item) noexcept
        {
            if (item.mesh == nullptr || item.material == nullptr)
            {
                return;
            }

            D3DCommandList& cmd = device.Commands();

            // 前回の描画ステートを引き継がないよう、描画ごとにパイプラインを設定する
            if (item.occludedOnly)
            {
                cmd.SetPipeline(device.OccludedPipeline(item.twoSided));
            }
            else
            {
                cmd.SetPipeline(device.CommonPipeline(item.blend, item.twoSided));
            }
            BindDrawItem(device, item);
            DrawMesh(cmd, *item.mesh);
        }

        void IssueDrawItemWith(D3DDevice& device,
                               const DrawItem& item,
                               const D3DPipeline& pipeline,
                               const D3DShader& pixelShader) noexcept
        {
            if (item.mesh == nullptr || item.material == nullptr)
            {
                return;
            }

            D3DCommandList& cmd = device.Commands();
            cmd.SetPipeline(pipeline);
            BindDrawItem(device, item);
            cmd.PSSetShader(pixelShader);
            DrawMesh(cmd, *item.mesh);
        }
    } // namespace Gfx
} // namespace NS
