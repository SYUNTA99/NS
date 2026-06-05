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
        void OnUpdate() override { ++updateCount; }
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

TEST(GameObjectTest, AddComponentAttachesOwnerAndAppendsToList)
{
    GameObject obj;
    auto& comp = *obj.AddComponent<MockComponent>();

    EXPECT_EQ(comp.Owner(), &obj);
    ASSERT_EQ(obj.Components().size(), std::size_t{1});
    EXPECT_EQ(obj.Components().front(), &comp);
}

TEST(GameObjectTest, OnUpdatePropagatesToActiveComponents)
{
    GameObject obj;
    auto& c1 = *obj.AddComponent<MockComponent>();
    auto& c2 = *obj.AddComponent<MockComponent>();

    obj.OnUpdate();
    EXPECT_EQ(c1.updateCount, 1);
    EXPECT_EQ(c2.updateCount, 1);
}

TEST(GameObjectTest, OnUpdateSkipsInactiveComponents)
{
    GameObject obj;
    auto& comp = *obj.AddComponent<MockComponent>();
    comp.SetActive(false);

    obj.OnUpdate();
    EXPECT_EQ(comp.updateCount, 0);
}

TEST(GameObjectTest, OnEndPlayCallsComponentsInReverseRegistrationOrder)
{
    GameObject obj;
    std::vector<int> callOrder;

    auto& a = *obj.AddComponent<OrderedComponent>();
    auto& b = *obj.AddComponent<OrderedComponent>();
    auto& c = *obj.AddComponent<OrderedComponent>();
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
        HighPrioComponent() noexcept : Component(static_cast<int>(NS::Scene::TickPriority::Input)) {}
    };

    class LowPrioComponent : public NS::Scene::Component
    {
    public:
        LowPrioComponent() noexcept : Component(static_cast<int>(NS::Scene::TickPriority::Camera)) {}
    };
} // namespace

TEST(GameObjectPriorityTest, AddComponentSortsByPriority)
{
    NS::Scene::GameObject obj;
    auto& low = *obj.AddComponent<LowPrioComponent>();   // 先に追加 (Camera, 400)
    auto& high = *obj.AddComponent<HighPrioComponent>(); // 後に追加 (Input, 0)

    ASSERT_EQ(obj.Components().size(), std::size_t{2});
    EXPECT_EQ(obj.Components()[0], &high); // priority 昇順で high 先
    EXPECT_EQ(obj.Components()[1], &low);
}

TEST(GameObjectPriorityTest, SamePriorityPreservesInsertionOrder)
{
    NS::Scene::GameObject obj;
    auto& a = *obj.AddComponent<HighPrioComponent>();
    auto& b = *obj.AddComponent<HighPrioComponent>();

    ASSERT_EQ(obj.Components().size(), std::size_t{2});
    EXPECT_EQ(obj.Components()[0], &a);
    EXPECT_EQ(obj.Components()[1], &b);
}

TEST(GameObjectAddComponentTest, OwnsLifetimeInjectsOwnerAndOrdersByPriority)
{
    NS::Scene::GameObject obj;
    auto* low = obj.AddComponent<LowPrioComponent>();   // Camera 400
    auto* high = obj.AddComponent<HighPrioComponent>(); // Input 0
    auto* mock = obj.AddComponent<MockComponent>();     // 既定 Physics 200

    ASSERT_NE(low, nullptr);
    ASSERT_NE(high, nullptr);
    ASSERT_NE(mock, nullptr);
    EXPECT_EQ(low->Owner(), &obj);
    EXPECT_EQ(high->Owner(), &obj);

    // 寿命は GameObject 所有: stack に持たなくても tick が伝播する
    obj.OnUpdate();
    EXPECT_EQ(mock->updateCount, 1);

    // priority 昇順 (Input 0 < Physics 200 < Camera 400)
    ASSERT_EQ(obj.Components().size(), std::size_t{3});
    EXPECT_EQ(obj.Components()[0], high);
    EXPECT_EQ(obj.Components()[1], mock);
    EXPECT_EQ(obj.Components()[2], low);
}
