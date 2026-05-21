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
    MockComponent comp;
    obj.RegisterComponent(&comp);

    EXPECT_EQ(comp.Owner(), &obj);
    ASSERT_EQ(obj.Components().size(), std::size_t{1});
    EXPECT_EQ(obj.Components().front(), &comp);
}

TEST(GameObjectTest, OnUpdatePropagatesToActiveComponents)
{
    GameObject obj;
    MockComponent c1;
    MockComponent c2;
    obj.RegisterComponent(&c1);
    obj.RegisterComponent(&c2);

    obj.OnUpdate(0.016f);
    EXPECT_EQ(c1.updateCount, 1);
    EXPECT_EQ(c2.updateCount, 1);
}

TEST(GameObjectTest, OnUpdateSkipsInactiveComponents)
{
    GameObject obj;
    MockComponent comp;
    obj.RegisterComponent(&comp);
    comp.SetActive(false);

    obj.OnUpdate(0.016f);
    EXPECT_EQ(comp.updateCount, 0);
}

TEST(GameObjectTest, OnEndPlayCallsComponentsInReverseRegistrationOrder)
{
    GameObject obj;
    std::vector<int> callOrder;

    OrderedComponent a, b, c;
    a.recorder = &callOrder;
    a.id = 1;
    b.recorder = &callOrder;
    b.id = 2;
    c.recorder = &callOrder;
    c.id = 3;

    obj.RegisterComponent(&a);
    obj.RegisterComponent(&b);
    obj.RegisterComponent(&c);
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
