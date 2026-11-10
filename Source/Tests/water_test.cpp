#include "Game/Level/WaterActor.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Gfx/Render/RenderBounds.h"

#include <gtest/gtest.h>

#include <memory>

// 使わない当たりを作らず、深度とキューブマップを読める段で描く
TEST(WaterActor, HasNoTerrainCollisionAndDrawsAfterTheSky)
{
    const std::unique_ptr<GL::Level::WaterActor> water = NS::CreateActor<GL::Level::WaterActor>();
    EXPECT_EQ(water->CollisionClassObj(), nullptr);
    ASSERT_NE(water->ModelClassObj(), nullptr);
    EXPECT_EQ(water->ModelClassObj()->Bounds(NS::Matrix::Identity).pass, NS::Gfx::RenderPass::AfterSky);
}

// 頂点シェーダーが動かす波の分をメッシュの範囲は含まないので、水面の近くで上を向いても間引かない
TEST(WaterActor, IsNeverCulled)
{
    const std::unique_ptr<GL::Level::WaterActor> water = NS::CreateActor<GL::Level::WaterActor>();
    const NS::AABB bounds = water->ModelClassObj()->Bounds(NS::Matrix::Identity).bounds;
    EXPECT_GE(bounds.Extents.x, 100000.0f);
    EXPECT_GE(bounds.Extents.y, 100000.0f);
    EXPECT_GE(bounds.Extents.z, 100000.0f);
}
