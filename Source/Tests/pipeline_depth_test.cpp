#include "Runtime/Graphics/Pipeline.h"
#include "Runtime/Graphics/detail/PipelineDepth.h"

#include <gtest/gtest.h>

// 隠れた所だけを描く深度は、手前の物より奥の画素だけを通し、深度を書かない
TEST(PipelineDepth, OccludedPassesOnlyPixelsBehindAndWritesNoDepth)
{
    const D3D11_DEPTH_STENCIL_DESC desc = NS::Gfx::detail::MakeDepthStencilDesc(NS::Gfx::DepthMode::Occluded);
    EXPECT_TRUE(desc.DepthEnable);
    EXPECT_EQ(desc.DepthFunc, D3D11_COMPARISON_GREATER);
    EXPECT_EQ(desc.DepthWriteMask, D3D11_DEPTH_WRITE_MASK_ZERO);
    EXPECT_FALSE(desc.StencilEnable);
}

// 今までの 3 つは比べ方と書き込みを変えない
TEST(PipelineDepth, ExistingModesKeepTheirComparison)
{
    const D3D11_DEPTH_STENCIL_DESC readWrite = NS::Gfx::detail::MakeDepthStencilDesc(NS::Gfx::DepthMode::ReadWrite);
    EXPECT_EQ(readWrite.DepthFunc, D3D11_COMPARISON_LESS);
    EXPECT_EQ(readWrite.DepthWriteMask, D3D11_DEPTH_WRITE_MASK_ALL);
    const D3D11_DEPTH_STENCIL_DESC readOnly = NS::Gfx::detail::MakeDepthStencilDesc(NS::Gfx::DepthMode::ReadOnly);
    EXPECT_EQ(readOnly.DepthFunc, D3D11_COMPARISON_LESS_EQUAL);
    EXPECT_EQ(readOnly.DepthWriteMask, D3D11_DEPTH_WRITE_MASK_ZERO);
    const D3D11_DEPTH_STENCIL_DESC disabled = NS::Gfx::detail::MakeDepthStencilDesc(NS::Gfx::DepthMode::Disabled);
    EXPECT_FALSE(disabled.DepthEnable);
}
