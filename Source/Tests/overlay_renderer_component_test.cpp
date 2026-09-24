#include <Runtime/Graphics/RenderContext.h>
#include <Runtime/Object/Components/OverlayRenderer.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Scene/Scene.h>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace
{
    // 描かれた回数を log の要素数で数える fake
    class FakeOverlay : public NS::Obj::OverlayRenderer
    {
    public:
        explicit FakeOverlay(std::vector<int>* log) : m_log(log) {}

        void OnRenderOverlay(const NS::Gfx::RenderContext&) override { m_log->push_back(1); }

        NS_REFLECT_NONE(FakeOverlay, NS::Obj::OverlayRenderer)

    private:
        std::vector<int>* m_log;
    };

    // OnStart を上書きする派生。基底を呼べば登録が効くことを見る
    class OverridingOverlay : public NS::Obj::OverlayRenderer
    {
    public:
        explicit OverridingOverlay(std::vector<int>* log) : m_log(log) {}

        void OnStart() override
        {
            NS::Obj::OverlayRenderer::OnStart();
            m_started = true;
        }

        void OnRenderOverlay(const NS::Gfx::RenderContext&) override { m_log->push_back(2); }

        [[nodiscard]] bool Started() const noexcept { return m_started; }

        NS_REFLECT_NONE(OverridingOverlay, NS::Obj::OverlayRenderer)

    private:
        std::vector<int>* m_log;
        bool m_started = false;
    };

    // protected の DrawOverlays を test から叩くための公開サブクラス
    class TestScene : public NS::Obj::Scene
    {
    public:
        using NS::Obj::Scene::DrawOverlays;
    };

    FakeOverlay* PlaceOverlay(TestScene& scene, std::vector<int>* log)
    {
        NS::Obj::GameObject* obj = scene.SpawnTransient(std::make_unique<NS::Obj::GameObject>());
        if (obj == nullptr)
        {
            return nullptr;
        }
        return obj->AddComponent<FakeOverlay>(log);
    }
} // namespace

TEST(OverlayRendererComponentTest, RegistersItselfOnStart)
{
    std::vector<int> log;
    TestScene scene;
    FakeOverlay* overlay = PlaceOverlay(scene, &log);
    ASSERT_NE(overlay, nullptr);

    overlay->OnStart();

    NS::Gfx::RenderContext ctx{};
    scene.DrawOverlays(ctx);
    EXPECT_EQ(log.size(), 1u);
}

TEST(OverlayRendererComponentTest, RegisteringTwiceDrawsOnce)
{
    std::vector<int> log;
    TestScene scene;
    FakeOverlay* overlay = PlaceOverlay(scene, &log);
    ASSERT_NE(overlay, nullptr);

    overlay->OnStart();
    overlay->OnStart();

    NS::Gfx::RenderContext ctx{};
    scene.DrawOverlays(ctx);
    EXPECT_EQ(log.size(), 1u);
}

TEST(OverlayRendererComponentTest, UnregistersOnEndPlay)
{
    std::vector<int> log;
    TestScene scene;
    FakeOverlay* overlay = PlaceOverlay(scene, &log);
    ASSERT_NE(overlay, nullptr);

    overlay->OnStart();
    overlay->OnEndPlay();

    NS::Gfx::RenderContext ctx{};
    scene.DrawOverlays(ctx);
    EXPECT_TRUE(log.empty());
}

TEST(OverlayRendererComponentTest, WithoutASceneNothingHappens)
{
    std::vector<int> log;
    NS::Obj::GameObject obj;
    FakeOverlay* overlay = obj.AddComponent<FakeOverlay>(&log);
    ASSERT_NE(overlay, nullptr);

    overlay->OnStart();
    overlay->OnEndPlay();

    EXPECT_TRUE(log.empty());
}

TEST(OverlayRendererComponentTest, DerivedThatOverridesOnStartStillRegisters)
{
    std::vector<int> log;
    TestScene scene;
    NS::Obj::GameObject* obj = scene.SpawnTransient(std::make_unique<NS::Obj::GameObject>());
    ASSERT_NE(obj, nullptr);
    OverridingOverlay* overlay = obj->AddComponent<OverridingOverlay>(&log);
    ASSERT_NE(overlay, nullptr);

    overlay->OnStart();

    NS::Gfx::RenderContext ctx{};
    scene.DrawOverlays(ctx);

    EXPECT_TRUE(overlay->Started());
    EXPECT_EQ(log.size(), 1u);
}
