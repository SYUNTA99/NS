#include <Runtime/Core/AABB.h>
#include <Runtime/Graphics/RenderContext.h>
#include <Runtime/Object/Components/OverlayRenderer.h>
#include <Runtime/Object/IRenderable.h>
#include <Runtime/Object/Scene/Scene.h>
#include <gtest/gtest.h>
#include <vector>

namespace
{
    using NS::Obj::IRenderable;
    using NS::Obj::RenderBucket;
    using NS::Gfx::RenderContext;
    using NS::Obj::Scene;

    // 描画された順に id を log へ積む fake。device 不要で bucket / sort のロジックだけ検証する
    class FakeRenderable : public IRenderable
    {
    public:
        FakeRenderable(int id, RenderBucket bucket, NS::Core::Vector3 center, int priority, std::vector<int>* log)
            : m_id(id), m_bucket(bucket), m_center(center), m_priority(priority), m_log(log)
        {
            // 既定は全域相当。順序・バケット検証のテストがカリングに邪魔されないようにする
            m_bounds = NS::Core::AABB{center, NS::Core::Vector3{1.0e6f, 1.0e6f, 1.0e6f}};
        }

        // カリングを検証するテストが視錐台内外へ置き直すための差し替え手段
        void SetWorldBounds(const NS::Core::AABB& bounds) noexcept { m_bounds = bounds; }

        void Collect(const RenderContext&, std::vector<NS::Gfx::DrawItem>&) override { m_log->push_back(m_id); }
        [[nodiscard]] RenderBucket Bucket() const noexcept override { return m_bucket; }
        [[nodiscard]] NS::Core::Vector3 SortCenter() const noexcept override { return m_center; }
        [[nodiscard]] int SortPriority() const noexcept override { return m_priority; }
        [[nodiscard]] NS::Core::AABB WorldBounds() const noexcept override { return m_bounds; }

    private:
        int m_id;
        RenderBucket m_bucket;
        NS::Core::Vector3 m_center;
        int m_priority;
        std::vector<int>* m_log;
        NS::Core::AABB m_bounds{};
    };

    // 描かれた順に id を log へ積む fake。priority 順と IsActive の扱いだけ検証する
    class FakeOverlay : public NS::Obj::OverlayRenderer
    {
    public:
        FakeOverlay(int id, int priority, std::vector<int>* log)
            : NS::Obj::OverlayRenderer(priority), m_id(id), m_log(log)
        {}

        void OnRenderOverlay(const RenderContext&) override { m_log->push_back(m_id); }

        NS_REFLECT_NONE(FakeOverlay, NS::Obj::OverlayRenderer)

    private:
        int m_id;
        std::vector<int>* m_log;
    };

    // protected の描画呼び出しを test から叩くための公開サブクラス
    class TestScene : public Scene
    {
    public:
        using Scene::DrawOpaque;
        using Scene::DrawOverlays;
        using Scene::DrawTransparent;
    };
} // namespace

TEST(SceneRenderQueue, DrawOpaqueDrawsOnlyOpaqueInRegistrationOrder)
{
    std::vector<int> log;
    FakeRenderable a(1, RenderBucket::Opaque, {}, 0, &log);
    FakeRenderable b(2, RenderBucket::Transparent, {}, 0, &log);
    FakeRenderable c(3, RenderBucket::Opaque, {}, 0, &log);

    TestScene scene;
    scene.RegisterRenderable(&a);
    scene.RegisterRenderable(&b);
    scene.RegisterRenderable(&c);

    RenderContext ctx{};
    scene.DrawOpaque(ctx);

    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], 1);
    EXPECT_EQ(log[1], 3);
}

TEST(SceneRenderQueue, DrawTransparentDrawsOnlyTransparent)
{
    std::vector<int> log;
    FakeRenderable a(1, RenderBucket::Opaque, {}, 0, &log);
    FakeRenderable b(2, RenderBucket::Transparent, {}, 0, &log);

    TestScene scene;
    scene.RegisterRenderable(&a);
    scene.RegisterRenderable(&b);

    RenderContext ctx{};
    scene.DrawTransparent(ctx);

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], 2);
}

TEST(SceneRenderQueue, TransparentSortedBackToFront)
{
    std::vector<int> log;
    FakeRenderable nearObj(10, RenderBucket::Transparent, {1.0f, 0.0f, 0.0f}, 0, &log);
    FakeRenderable midObj(20, RenderBucket::Transparent, {5.0f, 0.0f, 0.0f}, 0, &log);
    FakeRenderable farObj(30, RenderBucket::Transparent, {10.0f, 0.0f, 0.0f}, 0, &log);

    TestScene scene;
    // あえて near→far でない順で登録し、距離ソートが効くことを確認する
    scene.RegisterRenderable(&nearObj);
    scene.RegisterRenderable(&farObj);
    scene.RegisterRenderable(&midObj);

    RenderContext ctx{};
    ctx.cameraPosition = {0.0f, 0.0f, 0.0f};
    scene.DrawTransparent(ctx);

    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], 30); // 遠い順
    EXPECT_EQ(log[1], 20);
    EXPECT_EQ(log[2], 10);
}

TEST(SceneRenderQueue, TransparentTieBreakByPriorityThenRegistration)
{
    std::vector<int> log;
    // 全て camera から同距離 (x=5)。priority 昇順→同値は登録順
    FakeRenderable p1(1, RenderBucket::Transparent, {5.0f, 0.0f, 0.0f}, 5, &log);
    FakeRenderable p2(2, RenderBucket::Transparent, {5.0f, 0.0f, 0.0f}, 1, &log);
    FakeRenderable p3(3, RenderBucket::Transparent, {5.0f, 0.0f, 0.0f}, 5, &log);

    TestScene scene;
    scene.RegisterRenderable(&p1);
    scene.RegisterRenderable(&p2);
    scene.RegisterRenderable(&p3);

    RenderContext ctx{};
    ctx.cameraPosition = {0.0f, 0.0f, 0.0f};
    scene.DrawTransparent(ctx);

    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], 2); // priority 1 が先頭
    EXPECT_EQ(log[1], 1); // priority 5、登録順で p1
    EXPECT_EQ(log[2], 3); // priority 5、登録順で p3
}

namespace
{
    // identity viewProj の視錐台は [-1,1]x[-1,1]x[0,1]。その内外に AABB を置いて検証する
    NS::Core::AABB MakeBox(NS::Core::Vector3 center, float halfExtent)
    {
        return NS::Core::AABB{center, NS::Core::Vector3{halfExtent, halfExtent, halfExtent}};
    }
} // namespace

TEST(SceneRenderQueue, OpaqueOutsideFrustumIsCulled)
{
    std::vector<int> log;
    FakeRenderable inside(1, RenderBucket::Opaque, {}, 0, &log);
    FakeRenderable outside(2, RenderBucket::Opaque, {}, 0, &log);
    inside.SetWorldBounds(MakeBox({0.0f, 0.0f, 0.5f}, 0.1f));
    outside.SetWorldBounds(MakeBox({100.0f, 0.0f, 0.5f}, 0.1f)); // 右方向へ視錐台の外

    TestScene scene;
    scene.RegisterRenderable(&inside);
    scene.RegisterRenderable(&outside);

    RenderContext ctx{}; // viewProjection は identity
    scene.DrawOpaque(ctx);

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], 1); // 視錐台内の描画物だけが Collect される
}

TEST(SceneRenderQueue, TransparentOutsideFrustumIsCulled)
{
    std::vector<int> log;
    FakeRenderable inside(1, RenderBucket::Transparent, {0.0f, 0.0f, 0.5f}, 0, &log);
    FakeRenderable outside(2, RenderBucket::Transparent, {0.0f, 0.0f, 0.5f}, 0, &log);
    inside.SetWorldBounds(MakeBox({0.0f, 0.0f, 0.5f}, 0.1f));
    outside.SetWorldBounds(MakeBox({0.0f, 100.0f, 0.5f}, 0.1f)); // 上方向へ視錐台の外

    TestScene scene;
    scene.RegisterRenderable(&inside);
    scene.RegisterRenderable(&outside);

    RenderContext ctx{};
    scene.DrawTransparent(ctx);

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], 1); // 視錐台外はソート対象にもならない
}

TEST(SceneRenderQueue, OverlaysDrawInPriorityOrder)
{
    std::vector<int> log;
    FakeOverlay late(1, NS::Obj::TickPriority::LateUpdate + 5, &log);
    FakeOverlay early(2, NS::Obj::TickPriority::Update - 100, &log);

    TestScene scene;
    scene.RegisterOverlay(&late);
    scene.RegisterOverlay(&early);

    RenderContext ctx{};
    scene.DrawOverlays(ctx);

    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], 2);
    EXPECT_EQ(log[1], 1);
}

TEST(SceneRenderQueue, InactiveOverlayIsSkipped)
{
    std::vector<int> log;
    FakeOverlay shown(1, NS::Obj::TickPriority::Update, &log);
    FakeOverlay hidden(2, NS::Obj::TickPriority::Update, &log);
    hidden.SetActive(false);

    TestScene scene;
    scene.RegisterOverlay(&shown);
    scene.RegisterOverlay(&hidden);

    RenderContext ctx{};
    scene.DrawOverlays(ctx);

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], 1);
}
