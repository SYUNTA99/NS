#include <gtest/gtest.h>

#include <Framework/Core/Clock.h>
#include <Framework/Scene/SceneBase.h>

namespace
{
    class MockScene : public NS::Scene::SceneBase
    {
    public:
        int startCount = 0;
        int updateCount = 0;
        int renderCount = 0;
        int shutdownCount = 0;
        float dtAccum = 0.0f;

        void OnStart() override { ++startCount; }
        void OnUpdate() override
        {
            ++updateCount;
            dtAccum += NS::Core::FrameTimer::FixedDelta();
        }
        void OnRenderScene() override { ++renderCount; }
        void OnShutdown() override { ++shutdownCount; }
    };
} // namespace

TEST(SceneTest, DefaultCountersAreZero)
{
    MockScene scene;
    EXPECT_EQ(scene.startCount, 0);
    EXPECT_EQ(scene.updateCount, 0);
    EXPECT_EQ(scene.renderCount, 0);
    EXPECT_EQ(scene.shutdownCount, 0);
    EXPECT_FLOAT_EQ(scene.dtAccum, 0.0f);
}

TEST(SceneTest, OnUpdateAccumulatesDt)
{
    NS::Core::FrameTimer::SetFixedDelta(1.0f / 60.0f);
    MockScene scene;
    const float dt = NS::Core::FrameTimer::FixedDelta();
    scene.OnUpdate();
    scene.OnUpdate();
    scene.OnUpdate();

    EXPECT_EQ(scene.updateCount, 3);
    EXPECT_NEAR(scene.dtAccum, dt * 3.0f, 1e-5f);
}

TEST(SceneTest, LifecycleOrderIsIndependent)
{
    MockScene scene;
    scene.OnStart();
    scene.OnUpdate();
    scene.OnRender();
    scene.OnShutdown();

    EXPECT_EQ(scene.startCount, 1);
    EXPECT_EQ(scene.updateCount, 1);
    EXPECT_EQ(scene.renderCount, 1);
    EXPECT_EQ(scene.shutdownCount, 1);
}

TEST(SceneTest, BaseClassDefaultsAreNoop)
{
    NS::Scene::SceneBase scene;
    scene.OnStart();
    scene.OnUpdate();
    scene.OnRender();
    scene.OnShutdown();
    SUCCEED();
}

TEST(SceneTest, PolymorphicDeleteCallsDerivedDtor)
{
    bool dtorCalled = false;
    struct TrackedScene : public NS::Scene::SceneBase
    {
        bool* flag;
        explicit TrackedScene(bool* f) : flag(f) {}
        ~TrackedScene() override { *flag = true; }
    };
    {
        std::unique_ptr<NS::Scene::SceneBase> scene = std::make_unique<TrackedScene>(&dtorCalled);
    }
    EXPECT_TRUE(dtorCalled);
}
