#include <Runtime/Platform/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Core/Sphere.h>
#include <Runtime/Object/Components/BoxCollider.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Physics/PhysicsScene.h>
#include <gtest/gtest.h>

namespace
{
    class MockScene : public NS::Obj::Scene
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
            dtAccum += NS::Platform::FrameTimer::FixedDelta();
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
    NS::Platform::FrameTimer::SetFixedDelta(1.0f / 60.0f);
    MockScene scene;
    const float dt = NS::Platform::FrameTimer::FixedDelta();
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
    NS::Obj::Scene scene;
    scene.OnStart();
    scene.OnUpdate();
    scene.OnRender();
    scene.OnShutdown();
    SUCCEED();
}

TEST(SceneTest, PolymorphicDeleteCallsDerivedDtor)
{
    bool dtorCalled = false;
    struct TrackedScene : public NS::Obj::Scene
    {
        bool* flag;
        explicit TrackedScene(bool* f) : flag(f) {}
        ~TrackedScene() override { *flag = true; }
    };
    {
        std::unique_ptr<NS::Obj::Scene> scene = std::make_unique<TrackedScene>(&dtorCalled);
    }
    EXPECT_TRUE(dtorCalled);
}

TEST(SceneTest, DestroyObjectKeepsTheSurvivingColliderBodyId)
{
    NS::Obj::SceneData data;
    NS::Obj::ObjectData removed;
    removed.objectId = 10;
    removed.components.push_back(NS::Obj::MakeComponentEntry("BoxCollider"));
    data.objects.push_back(std::move(removed));

    NS::Obj::ObjectData survivor;
    survivor.objectId = 20;
    survivor.components.push_back(NS::Obj::MakeComponentEntry("BoxCollider"));
    data.objects.push_back(std::move(survivor));

    NS::Obj::Scene scene;
    scene.LoadFromData(std::move(data));
    NS::Obj::GameObject* survivingObject = scene.Objects().FindByObjectId(20);
    ASSERT_NE(survivingObject, nullptr);
    auto* collider = survivingObject->FindComponent<NS::Obj::BoxCollider>();
    ASSERT_NE(collider, nullptr);
    const JPH::BodyID bodyId = collider->BodyId();

    scene.DestroyObject(10);

    EXPECT_EQ(collider->BodyId(), bodyId);
    EXPECT_EQ(scene.Physics().BodyCount(), 1u);
}

namespace
{
    using NS::Core::Sphere;
    using NS::Core::Vector3;
    namespace ObjectLayers = NS::Phys::ObjectLayers;

    JPH::BodyID DropSphereInto(NS::Obj::Scene& scene)
    {
        const JPH::BodyID id =
            scene.Physics().AddDynamicSphere(Sphere{Vector3{0.0f, 10.0f, 0.0f}, 1.0f}, NS::Phys::DynamicBodyDesc{});
        scene.Physics().OptimizeBroadPhase();
        return id;
    }

    void RunFrames(NS::Obj::Scene& scene, int frames)
    {
        NS::Platform::FrameTimer::SetFixedDelta(1.0f / 60.0f);
        for (int i = 0; i < frames; ++i)
            scene.OnUpdate();
    }
} // namespace

TEST(SceneTest, OnUpdateStepsThePhysicsScene)
{
    NS::Obj::Scene scene;
    const JPH::BodyID id = DropSphereInto(scene);

    RunFrames(scene, 30);

    EXPECT_LT(scene.Physics().BodyPosition(id).y, 9.0f);
}

TEST(SceneTest, EditModeLeavesThePhysicsSceneStill)
{
    NS::Obj::Scene scene;
    const JPH::BodyID id = DropSphereInto(scene);
    scene.SetSimulationEnabled(false);

    RunFrames(scene, 30);

    EXPECT_NEAR(scene.Physics().BodyPosition(id).y, 10.0f, 1.0e-5f);
}

TEST(SceneTest, PausedSceneLeavesThePhysicsSceneStill)
{
    NS::Obj::Scene scene;
    const JPH::BodyID id = DropSphereInto(scene);
    scene.SetSimulationPaused(true);

    RunFrames(scene, 30);

    EXPECT_NEAR(scene.Physics().BodyPosition(id).y, 10.0f, 1.0e-5f);
}

namespace
{
    class MoverComponent : public NS::Obj::Component
    {
    public:
        void OnUpdate() override
        {
            NS::Obj::Transform& root = Owner()->Root();
            root.SetPosition(root.Position() + Vector3{1.0f, 0.0f, 0.0f});
        }
    };
} // namespace

TEST(SceneTest, UpdateLeavesThePreviousStepForInterpolation)
{
    NS::Platform::FrameTimer::SetFixedDelta(1.0f / 60.0f);
    NS::Obj::Scene scene;
    NS::Obj::GameObject* obj = scene.SpawnTransient<NS::Obj::GameObject>();
    ASSERT_NE(obj, nullptr);
    obj->AddComponent<MoverComponent>();

    scene.OnUpdate();
    scene.OnUpdate();

    const NS::Core::Matrix half = obj->Root().InterpolatedWorldMatrix(0.5f);
    EXPECT_NEAR(half.Translation().x, 1.5f, 1.0e-4f);

    const NS::Core::Matrix current = obj->Root().InterpolatedWorldMatrix(1.0f);
    EXPECT_NEAR(current.Translation().x, 2.0f, 1.0e-4f);
}

TEST(SceneTest, PausedSceneFreezesTheInterpolation)
{
    NS::Platform::FrameTimer::SetFixedDelta(1.0f / 60.0f);
    NS::Obj::Scene scene;
    NS::Obj::GameObject* obj = scene.SpawnTransient<NS::Obj::GameObject>();
    ASSERT_NE(obj, nullptr);
    obj->AddComponent<MoverComponent>();

    scene.OnUpdate();
    scene.SetSimulationPaused(true);
    scene.OnUpdate();

    const NS::Core::Matrix half = obj->Root().InterpolatedWorldMatrix(0.5f);
    EXPECT_NEAR(half.Translation().x, 1.0f, 1.0e-4f);
}
