#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Physics/JoltWorld.h>
#include <gtest/gtest.h>

namespace
{
    class MockScene : public NS::Object::Scene
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
    NS::Object::Scene scene;
    scene.OnStart();
    scene.OnUpdate();
    scene.OnRender();
    scene.OnShutdown();
    SUCCEED();
}

TEST(SceneTest, PolymorphicDeleteCallsDerivedDtor)
{
    bool dtorCalled = false;
    struct TrackedScene : public NS::Object::Scene
    {
        bool* flag;
        explicit TrackedScene(bool* f) : flag(f) {}
        ~TrackedScene() override { *flag = true; }
    };
    {
        std::unique_ptr<NS::Object::Scene> scene = std::make_unique<TrackedScene>(&dtorCalled);
    }
    EXPECT_TRUE(dtorCalled);
}

namespace
{
    using NS::Core::Sphere;
    using NS::Core::Vector3;
    namespace ObjectLayers = NS::Physics::ObjectLayers;

    JPH::BodyID DropSphereInto(NS::Object::Scene& scene)
    {
        const JPH::BodyID id = scene.Jolt().AddSphere(Sphere{Vector3{0.0f, 10.0f, 0.0f}, 1.0f}, ObjectLayers::Rock);
        scene.Jolt().OptimizeBroadPhase();
        scene.Jolt().SetBodyDynamic(id, true);
        return id;
    }

    void RunFrames(NS::Object::Scene& scene, int frames)
    {
        NS::Core::FrameTimer::SetFixedDelta(1.0f / 60.0f);
        for (int i = 0; i < frames; ++i)
            scene.OnUpdate();
    }
} // namespace

TEST(SceneTest, OnUpdateStepsTheJoltWorld)
{
    NS::Object::Scene scene;
    const JPH::BodyID id = DropSphereInto(scene);

    RunFrames(scene, 30);

    EXPECT_LT(scene.Jolt().BodyPosition(id).y, 9.0f);
}

TEST(SceneTest, EditModeLeavesTheJoltWorldStill)
{
    NS::Object::Scene scene;
    const JPH::BodyID id = DropSphereInto(scene);
    scene.SetSimulationEnabled(false);

    RunFrames(scene, 30);

    EXPECT_NEAR(scene.Jolt().BodyPosition(id).y, 10.0f, 1.0e-5f);
}

TEST(SceneTest, PausedSceneLeavesTheJoltWorldStill)
{
    NS::Object::Scene scene;
    const JPH::BodyID id = DropSphereInto(scene);
    scene.SetSimulationPaused(true);

    RunFrames(scene, 30);

    EXPECT_NEAR(scene.Jolt().BodyPosition(id).y, 10.0f, 1.0e-5f);
}
