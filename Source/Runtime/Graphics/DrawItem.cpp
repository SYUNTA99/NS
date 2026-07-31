#include "Runtime/Graphics/DrawItem.h"

#include "Runtime/Graphics/CommandList.h"
#include "Runtime/Graphics/Material.h"
#include "Runtime/Graphics/Mesh.h"
#include "Runtime/Graphics/Renderer.h"

namespace NS::Graphics
{
    void IssueDrawItem(Renderer& renderer, const DrawItem& item) noexcept
    {
        if (item.mesh == nullptr || item.material == nullptr)
            return;

        CommandList& cmd = renderer.Commands();

        // 前回の描画ステートを引き継がないよう、毎回必ずパイプラインを初期化・設定する
        cmd.SetPipeline(renderer.CommonPipeline(item.blend));
        item.material->CreateInputLayoutFor(*item.mesh);
        item.material->SetParams(renderer, item.constants);

        // 追加のシェーダデータ（アニメーション情報など）があれば転送・適用する
        if (item.extraVsCb != nullptr)
        {
            if (item.extraVsData != nullptr && item.extraVsSize != 0)
                cmd.UpdateSubresource(*item.extraVsCb, item.extraVsData, item.extraVsSize);
            cmd.VSSetConstantBuffer(*item.extraVsCb, item.extraVsSlot);
        }

        item.material->Bind(renderer);
        DrawMesh(cmd, *item.mesh);
    }

} // namespace NS::Graphics
