#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Gfx/D3DPipeline.h"
#include "NSlib/Gfx/detail/CommonPipelines.h"

#include <gtest/gtest.h>

#include <memory>

// 装置の無い場ではパイプラインが無効になるので、作れなかった事を false で返す
TEST(CommonPipelines, ReportsFailureWhenAPipelineIsInvalid)
{
    ASSERT_EQ(NS::Gfx::Gpu().device, nullptr);
    std::unique_ptr<NS::Gfx::D3DPipeline> common[2][3];
    std::unique_ptr<NS::Gfx::D3DPipeline> occluded[2];

    EXPECT_FALSE(NS::Gfx::detail::CreateCommonPipelines(common, occluded));
}
