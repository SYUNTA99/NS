#include "Game/Level/WaterActor.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Gfx/Render/RenderContext.h"
#include "NSlib/Gfx/Render/RenderProxyList.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

// 使わない当たりを作らず、深度とキューブマップを読める段で描く
TEST(WaterActor, HasNoTerrainCollisionAndDrawsAfterTheSky)
{
    const std::unique_ptr<GL::Level::WaterActor> water = NS::CreateActor<GL::Level::WaterActor>();
    EXPECT_EQ(water->CollisionClassObj(), nullptr);
    ASSERT_NE(water->ModelClassObj(), nullptr);
    EXPECT_EQ(water->ModelClassObj()->Bucket(), NS::Gfx::RenderPass::AfterSky);
}

// 頂点シェーダーが動かす波の分をメッシュの範囲は含まないので、水面の近くで上を向いても間引かない
TEST(WaterActor, IsNeverCulled)
{
    const std::unique_ptr<GL::Level::WaterActor> water = NS::CreateActor<GL::Level::WaterActor>();
    const NS::AABB bounds = water->ModelClassObj()->WorldBounds(NS::RenderSource{});
    EXPECT_GE(bounds.Extents.x, 100000.0f);
    EXPECT_GE(bounds.Extents.y, 100000.0f);
    EXPECT_GE(bounds.Extents.z, 100000.0f);
}

namespace
{
    void CountCollect(void* owner, const NS::Gfx::RenderContext&, std::vector<NS::Gfx::DrawItem>&)
    {
        ++*static_cast<int*>(owner);
    }

    void Add(NS::Gfx::RenderProxyList& list, NS::Gfx::RenderPass pass, int& count)
    {
        NS::Gfx::RenderProxyDesc desc{};
        desc.bounds = NS::AABB(NS::Vector3::Zero, NS::Vector3::One);
        desc.pass = pass;
        desc.collect = &CountCollect;
        desc.owner = &count;
        (void)list.Register(desc);
    }
} // namespace

// 段ごとに分けて描き、他の段の物を呼ばない
TEST(RenderProxyList, DrawsOnlyTheRequestedPass)
{
    int opaque = 0;
    int afterSky = 0;
    int transparent = 0;
    NS::Gfx::RenderProxyList list;
    EXPECT_FALSE(list.HasPass(NS::Gfx::RenderPass::AfterSky));
    Add(list, NS::Gfx::RenderPass::Opaque, opaque);
    Add(list, NS::Gfx::RenderPass::AfterSky, afterSky);
    Add(list, NS::Gfx::RenderPass::Transparent, transparent);
    EXPECT_TRUE(list.HasPass(NS::Gfx::RenderPass::AfterSky));

    NS::Gfx::RenderContext context{};
    context.viewProjection = NS::Matrix::CreateOrthographic(100.0f, 100.0f, -100.0f, 100.0f);
    list.DrawBucket(context, NS::Gfx::RenderPass::AfterSky);
    EXPECT_EQ(opaque, 0);
    EXPECT_EQ(afterSky, 1);
    EXPECT_EQ(transparent, 0);
}
