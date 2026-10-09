#include "NSlib/Graphics/DrawItem.h"

#include "NSlib/Graphics/CommandList.h"
#include "NSlib/Graphics/Material.h"
#include "NSlib/Graphics/Mesh.h"
#include "NSlib/Graphics/Renderer.h"
#include "NSlib/Graphics/Shader.h"

namespace NS::Gfx
{
    namespace
    {
        // パイプラインは呼ぶ前に設定する
        void BindDrawItem(Renderer& renderer, const DrawItem& item) noexcept
        {
            CommandList& cmd = renderer.Commands();
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

        CommandList& cmd = renderer.Commands();

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
                           const Pipeline& pipeline,
                           const Shader& pixelShader) noexcept
    {
        if (item.mesh == nullptr || item.material == nullptr)
        {
            return;
        }

        CommandList& cmd = renderer.Commands();
        cmd.SetPipeline(pipeline);
        BindDrawItem(renderer, item);
        cmd.PSSetShader(pixelShader);
        DrawMesh(cmd, *item.mesh);
    }

} // namespace NS::Gfx
