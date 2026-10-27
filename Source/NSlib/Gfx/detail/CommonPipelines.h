#pragma once

#include "NSlib/Gfx/D3DPipeline.h"

#include <memory>

namespace NS
{
    namespace Gfx
    {
        namespace detail
        {
            //! D3DPipeline::Create は Gpu() を参照するため、D3DGlobal 公開後に呼ぶこと
            //! 第 1 添え字 [0] は裏面カリング、[1] は両面描画。outCommon の第 2 添え字は不透明・半透明・加算
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
