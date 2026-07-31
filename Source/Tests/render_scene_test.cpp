#include <gtest/gtest.h>
#include <Runtime/Graphics/RenderContext.h>
#include <Runtime/Graphics/RenderScene.h>
#include <vector>

namespace
{
    using NS::Graphics::DrawItem;
    using NS::Graphics::RenderContext;
    using NS::Graphics::RenderHandle;
    using NS::Graphics::RenderProxyDesc;
    using NS::Graphics::RenderScene;

    // collect で id を log に積むだけの owner。device 不要でカリング/順序/バケットを検証する
    struct Logger
    {
        int id = 0;
        std::vector<int>* log = nullptr;
    };

    void LogCollect(void* owner, const RenderContext&, std::vector<DrawItem>&)
    {
        auto* logger = static_cast<Logger*>(owner);
        logger->log->push_back(logger->id);
    }

    NS::Math::AABB MakeBox(NS::Math::Vector3 center, float halfExtent)
    {
        return NS::Math::AABB{center, NS::Math::Vector3{halfExtent, halfExtent, halfExtent}};
    }

    // 既定 bounds は全域相当でカリングに引っかからない。順序/バケット検証用
    RenderProxyDesc MakeDesc(Logger* logger, bool transparent, NS::Math::Vector3 sortCenter, int priority)
    {
        RenderProxyDesc d{};
        d.bounds = MakeBox({0.0f, 0.0f, 0.0f}, 1.0e6f);
        d.sortCenter = sortCenter;
        d.sortPriority = priority;
        d.transparent = transparent;
        d.collect = &LogCollect;
        d.owner = logger;
        return d;
    }
} // namespace

TEST(RenderSceneTest, DrawBucketDrawsOnlyMatchingBucketInRegistrationOrder)
{
    std::vector<int> log;
    Logger a{1, &log}, b{2, &log}, c{3, &log};
    RenderScene scene;
    (void)scene.Register(MakeDesc(&a, false, {}, 0)); // opaque
    (void)scene.Register(MakeDesc(&b, true, {}, 0));  // transparent
    (void)scene.Register(MakeDesc(&c, false, {}, 0)); // opaque

    RenderContext ctx{};
    scene.DrawBucket(ctx, false);

    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], 1); // opaque を登録順に、transparent は混ざらない
    EXPECT_EQ(log[1], 3);
}

TEST(RenderSceneTest, TransparentSortedBackToFront)
{
    std::vector<int> log;
    Logger nearObj{10, &log}, midObj{20, &log}, farObj{30, &log};
    RenderScene scene;
    // あえて near→far でない順で登録し、距離ソートが効くことを確認する
    (void)scene.Register(MakeDesc(&nearObj, true, {1.0f, 0.0f, 0.0f}, 0));
    (void)scene.Register(MakeDesc(&farObj, true, {10.0f, 0.0f, 0.0f}, 0));
    (void)scene.Register(MakeDesc(&midObj, true, {5.0f, 0.0f, 0.0f}, 0));

    RenderContext ctx{};
    ctx.cameraPosition = {0.0f, 0.0f, 0.0f};
    scene.DrawBucket(ctx, true);

    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], 30); // 遠い順
    EXPECT_EQ(log[1], 20);
    EXPECT_EQ(log[2], 10);
}

TEST(RenderSceneTest, TransparentTieBreakByPriorityThenRegistration)
{
    std::vector<int> log;
    // 全て camera から同距離 (x=5)。priority 昇順→同値は登録順
    Logger p1{1, &log}, p2{2, &log}, p3{3, &log};
    RenderScene scene;
    (void)scene.Register(MakeDesc(&p1, true, {5.0f, 0.0f, 0.0f}, 5));
    (void)scene.Register(MakeDesc(&p2, true, {5.0f, 0.0f, 0.0f}, 1));
    (void)scene.Register(MakeDesc(&p3, true, {5.0f, 0.0f, 0.0f}, 5));

    RenderContext ctx{};
    ctx.cameraPosition = {0.0f, 0.0f, 0.0f};
    scene.DrawBucket(ctx, true);

    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], 2); // priority 1 が先頭
    EXPECT_EQ(log[1], 1); // priority 5、登録順で p1
    EXPECT_EQ(log[2], 3); // priority 5、登録順で p3
}

TEST(RenderSceneTest, OutsideFrustumIsCulled)
{
    std::vector<int> log;
    Logger inside{1, &log}, outside{2, &log};
    RenderScene scene;
    RenderProxyDesc di = MakeDesc(&inside, false, {}, 0);
    di.bounds = MakeBox({0.0f, 0.0f, 0.5f}, 0.1f); // identity 視錐台 [-1,1]x[-1,1]x[0,1] の内
    RenderProxyDesc dOut = MakeDesc(&outside, false, {}, 0);
    dOut.bounds = MakeBox({100.0f, 0.0f, 0.5f}, 0.1f); // 右方向へ視錐台の外
    (void)scene.Register(di);
    (void)scene.Register(dOut);

    RenderContext ctx{}; // viewProjection は identity
    scene.DrawBucket(ctx, false);

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], 1); // 視錐台内だけ collect される
}

TEST(RenderSceneTest, UnregisterStopsDrawingAndIgnoresStaleHandle)
{
    std::vector<int> log;
    Logger a{1, &log}, b{2, &log};
    RenderScene scene;
    const RenderHandle ha = scene.Register(MakeDesc(&a, false, {}, 0));
    (void)scene.Register(MakeDesc(&b, false, {}, 0));
    EXPECT_EQ(scene.Count(), 2u);

    scene.Unregister(ha);
    EXPECT_EQ(scene.Count(), 1u);

    RenderContext ctx{};
    scene.DrawBucket(ctx, false);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], 2); // 解除した a は描かれない

    // 解除済みハンドルの再操作は無視される
    scene.Unregister(ha);
    scene.Update(ha, MakeBox({}, 1.0f), {}, 0, false);
    EXPECT_EQ(scene.Count(), 1u);
}

TEST(RenderSceneTest, SlotReuseInvalidatesOldHandle)
{
    RenderScene scene;
    Logger a{1, nullptr}, b{2, nullptr};
    const RenderHandle ha = scene.Register(MakeDesc(&a, false, {}, 0));
    scene.Unregister(ha);
    const RenderHandle hb = scene.Register(MakeDesc(&b, false, {}, 0)); // 空きスロット再利用

    EXPECT_EQ(hb.slot, ha.slot);             // 同じ slot を再利用
    EXPECT_NE(hb.generation, ha.generation); // 世代は進んでいる
    EXPECT_TRUE(hb.IsValid());
    EXPECT_EQ(scene.Count(), 1u);

    scene.Unregister(ha); // 古いハンドルでは新 proxy を消さない
    EXPECT_EQ(scene.Count(), 1u);
}
