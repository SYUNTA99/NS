#include <gtest/gtest.h>

#include <Framework/Scene/Component.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/Transform.h>

#include <vector>

namespace
{
    using NS::Scene::Component;
    using NS::Scene::GameObject;

    class MockComponent : public Component
    {
    public:
        using Component::Component;
        int startCount = 0;
        int updateCount = 0;
        int endPlayCount = 0;
        void OnStart() override { ++startCount; }
        void OnUpdate(float) override { ++updateCount; }
        void OnEndPlay() override { ++endPlayCount; }
    };

    class OrderedComponent : public Component
    {
    public:
        using Component::Component;
        std::vector<int>* recorder = nullptr;
        int id = 0;
        void OnEndPlay() override
        {
            if (recorder != nullptr)
                recorder->push_back(id);
        }
    };
} // namespace

TEST(GameObjectTest, RegisterComponentAttachesOwnerAndAppendsToList)
{
    GameObject obj;
    MockComponent comp(&obj);

    EXPECT_EQ(comp.Owner(), &obj);
    ASSERT_EQ(obj.Components().size(), std::size_t{1});
    EXPECT_EQ(obj.Components().front(), &comp);
}

TEST(GameObjectTest, OnUpdatePropagatesToActiveComponents)
{
    GameObject obj;
    MockComponent c1(&obj);
    MockComponent c2(&obj);

    obj.OnUpdate(0.016f);
    EXPECT_EQ(c1.updateCount, 1);
    EXPECT_EQ(c2.updateCount, 1);
}

TEST(GameObjectTest, OnUpdateSkipsInactiveComponents)
{
    GameObject obj;
    MockComponent comp(&obj);
    comp.SetActive(false);

    obj.OnUpdate(0.016f);
    EXPECT_EQ(comp.updateCount, 0);
}

TEST(GameObjectTest, OnEndPlayCallsComponentsInReverseRegistrationOrder)
{
    GameObject obj;
    std::vector<int> callOrder;

    OrderedComponent a(&obj), b(&obj), c(&obj);
    a.recorder = &callOrder;
    a.id = 1;
    b.recorder = &callOrder;
    b.id = 2;
    c.recorder = &callOrder;
    c.id = 3;

    obj.OnEndPlay();

    ASSERT_EQ(callOrder.size(), std::size_t{3});
    EXPECT_EQ(callOrder[0], 3);
    EXPECT_EQ(callOrder[1], 2);
    EXPECT_EQ(callOrder[2], 1);
}

TEST(GameObjectTest, SetParentLinksHierarchyAndSyncsTransform)
{
    GameObject parent;
    GameObject child;
    child.SetParent(&parent);

    EXPECT_EQ(child.Parent(), &parent);
    ASSERT_EQ(parent.Children().size(), std::size_t{1});
    EXPECT_EQ(parent.Children().front(), &child);
    EXPECT_EQ(child.Root().Parent(), &parent.Root());

    child.SetParent(nullptr);
    EXPECT_EQ(child.Parent(), nullptr);
    EXPECT_TRUE(parent.Children().empty());
    EXPECT_EQ(child.Root().Parent(), nullptr);
}

TEST(GameObjectTest, MarkPendingKillFlipsIsAlive)
{
    GameObject obj;
    EXPECT_TRUE(obj.IsAlive());
    obj.MarkPendingKill();
    EXPECT_FALSE(obj.IsAlive());
}

namespace
{
    class HighPrioComponent : public NS::Scene::Component
    {
    public:
        HighPrioComponent() noexcept : Component(nullptr, static_cast<int>(NS::Scene::TickPriority::Input)) {}
    };

    class LowPrioComponent : public NS::Scene::Component
    {
    public:
        LowPrioComponent() noexcept : Component(nullptr, static_cast<int>(NS::Scene::TickPriority::Camera)) {}
    };
} // namespace

TEST(GameObjectPriorityTest, RegisterComponentSortsByPriority)
{
    NS::Scene::GameObject obj;
    LowPrioComponent low;
    HighPrioComponent high;

    obj.RegisterComponent(&low);
    obj.RegisterComponent(&high);

    ASSERT_EQ(obj.Components().size(), std::size_t{2});
    EXPECT_EQ(obj.Components()[0], &high);
    EXPECT_EQ(obj.Components()[1], &low);
}

TEST(GameObjectPriorityTest, SamePriorityPreservesInsertionOrder)
{
    NS::Scene::GameObject obj;
    HighPrioComponent a;
    HighPrioComponent b;

    obj.RegisterComponent(&a);
    obj.RegisterComponent(&b);

    ASSERT_EQ(obj.Components().size(), std::size_t{2});
    EXPECT_EQ(obj.Components()[0], &a);
    EXPECT_EQ(obj.Components()[1], &b);
}
