#include <gtest/gtest.h>

#include <ns/app/scene.h>

namespace
{
    class MockScene : public ns::app::Scene
    {
    public:
        int startCount = 0;
        int updateCount = 0;
        int renderCount = 0;
        int shutdownCount = 0;
        float dtAccum = 0.0f;

        void OnStart() override { ++startCount; }
        void OnUpdate(float dt) override
        {
            ++updateCount;
            dtAccum += dt;
        }
        void OnRender() override { ++renderCount; }
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
    MockScene scene;
    constexpr float dt = 1.0f / 60.0f;
    scene.OnUpdate(dt);
    scene.OnUpdate(dt);
    scene.OnUpdate(dt);

    EXPECT_EQ(scene.updateCount, 3);
    EXPECT_NEAR(scene.dtAccum, dt * 3.0f, 1e-5f);
}

TEST(SceneTest, LifecycleOrderIsIndependent)
{
    MockScene scene;
    scene.OnStart();
    scene.OnUpdate(1.0f / 60.0f);
    scene.OnRender();
    scene.OnShutdown();

    EXPECT_EQ(scene.startCount, 1);
    EXPECT_EQ(scene.updateCount, 1);
    EXPECT_EQ(scene.renderCount, 1);
    EXPECT_EQ(scene.shutdownCount, 1);
}

TEST(SceneTest, BaseClassDefaultsAreNoop)
{
    ns::app::Scene scene;
    scene.OnStart();
    scene.OnUpdate(1.0f / 60.0f);
    scene.OnRender();
    scene.OnShutdown();
    SUCCEED();
}

TEST(SceneTest, PolymorphicDeleteCallsDerivedDtor)
{
    bool dtorCalled = false;
    struct TrackedScene : public ns::app::Scene
    {
        bool* flag;
        explicit TrackedScene(bool* f) : flag(f) {}
        ~TrackedScene() override { *flag = true; }
    };
    {
        std::unique_ptr<ns::app::Scene> scene = std::make_unique<TrackedScene>(&dtorCalled);
    }
    EXPECT_TRUE(dtorCalled);
}
