#include <Runtime/Core/AABB.h>
#include <Runtime/Graphics/RenderContext.h>
#include <Runtime/Object/IRenderable.h>
#include <Runtime/Object/Scene/SceneRenderer.h>
#include <gtest/gtest.h>
#include <vector>

namespace
{
    using NS::Graphics::RenderContext;
    using NS::Object::IRenderable;
    using NS::Object::RenderBucket;
    using NS::Object::SceneRenderer;

    class FakeRenderable : public IRenderable
    {
    public:
        FakeRenderable(int id, std::vector<int>* log) : m_id(id), m_log(log) {}

        void Collect(const RenderContext&, std::vector<NS::Graphics::DrawItem>&) override { m_log->push_back(m_id); }
        [[nodiscard]] RenderBucket Bucket() const noexcept override { return RenderBucket::Opaque; }
        [[nodiscard]] NS::Core::Vector3 SortCenter() const noexcept override { return NS::Core::Vector3{}; }
        [[nodiscard]] int SortPriority() const noexcept override { return 0; }
        [[nodiscard]] NS::Core::AABB WorldBounds() const noexcept override
        {
            // 視錐台に入れるため原点を囲む
            // 既定の RenderContext では視錐台がクリップ空間そのものになる
            return NS::Core::AABB{NS::Core::Vector3{}, NS::Core::Vector3{1.0f, 1.0f, 1.0f}};
        }

    private:
        int m_id;
        std::vector<int>* m_log;
    };
} // namespace

TEST(SceneRendererTest, DrawsRegisteredRenderableWithoutAScene)
{
    std::vector<int> log;
    FakeRenderable renderable{7, &log};

    SceneRenderer renderer;
    renderer.RegisterRenderable(&renderable);
    renderer.SyncRenderBounds();

    RenderContext ctx{};
    renderer.DrawOpaque(ctx);

    EXPECT_EQ(log.size(), 1u);
}

TEST(SceneRendererTest, UnregisteredRenderableIsNotDrawn)
{
    std::vector<int> log;
    FakeRenderable renderable{7, &log};

    SceneRenderer renderer;
    renderer.RegisterRenderable(&renderable);
    renderer.UnregisterRenderable(&renderable);
    renderer.SyncRenderBounds();

    RenderContext ctx{};
    renderer.DrawOpaque(ctx);

    EXPECT_TRUE(log.empty());
}
