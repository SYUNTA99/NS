#pragma once

// D3DDevice が構築の最後に作る共通パイプラインを組む内部ヘルパ

#include "NSlib/Gfx/D3DPipeline.h"

#include <memory>

namespace NS
{
    namespace Gfx
    {
        namespace detail
        {
            //! @brief 共通パイプライン 8 本を作り、全部が有効なら true を返す
            //! @details D3DPipeline::Create は Gpu() を引くので、装置を公開した後に呼ぶ。
            //! 添え字の [0] は裏を向いた面を描かない、[1] は裏も描く。outCommon の 2 つ目の添え字は不透明・半透明・加算
            [[nodiscard]] inline bool CreateCommonPipelines(std::unique_ptr<D3DPipeline> (&outCommon)[2][3],
                                                            std::unique_ptr<D3DPipeline> (&outOccluded)[2])
            {
                const CullMode culls[2] = {CullMode::Back, CullMode::None};
                bool allValid = true;
                for (int sided = 0; sided < 2; ++sided)
                {
                    const CullMode cull = culls[sided];
                    outCommon[sided][0] = D3DPipeline::Create(D3DPipelineDesc{.cull = cull});
                    outCommon[sided][1] = D3DPipeline::Create(
                        D3DPipelineDesc{.cull = cull, .blend = BlendMode::Alpha, .depth = DepthMode::ReadOnly});
                    outCommon[sided][2] = D3DPipeline::Create(
                        D3DPipelineDesc{.cull = cull, .blend = BlendMode::Additive, .depth = DepthMode::ReadOnly});
                    outOccluded[sided] = D3DPipeline::Create(
                        D3DPipelineDesc{.cull = cull, .blend = BlendMode::Alpha, .depth = DepthMode::Occluded});
                    for (const std::unique_ptr<D3DPipeline>& pipeline : outCommon[sided])
                    {
                        allValid = allValid && pipeline->IsValid();
                    }
                    allValid = allValid && outOccluded[sided]->IsValid();
                }
                return allValid;
            }
        } // namespace detail
    } // namespace Gfx
} // namespace NS
