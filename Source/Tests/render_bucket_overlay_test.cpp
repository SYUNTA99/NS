#include <gtest/gtest.h>

#include <Framework/Scene/IRenderable.h>
#include <Framework/Scene/RenderContext.h>
#include <Framework/Scene/SceneBase.h>

#include <vector>

namespace
{
    // 描画呼び出しを記録する spy。バケットは構築時に固定する
    class SpyRenderable : public NS::Scene::IRenderable
    {
    public:
        SpyRenderable(NS::Scene::RenderBucket bucket, std::vector<const SpyRenderable*>* log) noexcept
            : m_bucket(bucket), m_log(log)
        {}

        void Draw(const NS::Scene::RenderContext&) override { m_log->push_back(this); }
        [[nodiscard]] NS::Scene::RenderBucket Bucket() const noexcept override { return m_bucket; }

    private:
        NS::Scene::RenderBucket m_bucket;
        std::vector<const SpyRenderable*>* m_log;
    };

    // protected の Draw 系をテストへ公開する
    class TestScene : public NS::Scene::SceneBase
    {
    public:
        using NS::Scene::SceneBase::DrawOpaque;
        using NS::Scene::SceneBase::DrawOverlay;
        using NS::Scene::SceneBase::DrawTransparent;
    };
} // namespace

TEST(RenderBucketOverlayTest, OverlayNotDrawnByOpaqueOrTransparent)
{
    TestScene scene;
    std::vector<const SpyRenderable*> log;
    SpyRenderable opaque{NS::Scene::RenderBucket::Opaque, &log};
    SpyRenderable transparent{NS::Scene::RenderBucket::Transparent, &log};
    SpyRenderable overlay{NS::Scene::RenderBucket::Overlay, &log};
    scene.RegisterRenderable(&opaque);
    scene.RegisterRenderable(&transparent);
    scene.RegisterRenderable(&overlay);

    const NS::Scene::RenderContext ctx{};
    scene.DrawOpaque(ctx);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], &opaque);

    log.clear();
    scene.DrawTransparent(ctx);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], &transparent);
}

TEST(RenderBucketOverlayTest, DrawOverlayDrawsOnlyOverlay)
{
    TestScene scene;
    std::vector<const SpyRenderable*> log;
    SpyRenderable opaque{NS::Scene::RenderBucket::Opaque, &log};
    SpyRenderable overlay{NS::Scene::RenderBucket::Overlay, &log};
    scene.RegisterRenderable(&opaque);
    scene.RegisterRenderable(&overlay);

    const NS::Scene::RenderContext ctx{};
    scene.DrawOverlay(ctx);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], &overlay);
}

TEST(RenderBucketOverlayTest, OverlayDrawsInRegistrationOrder)
{
    TestScene scene;
    std::vector<const SpyRenderable*> log;
    SpyRenderable first{NS::Scene::RenderBucket::Overlay, &log};
    SpyRenderable second{NS::Scene::RenderBucket::Overlay, &log};
    scene.RegisterRenderable(&first);
    scene.RegisterRenderable(&second);

    const NS::Scene::RenderContext ctx{};
    scene.DrawOverlay(ctx);
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], &first);
    EXPECT_EQ(log[1], &second);
}
