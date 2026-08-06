#include <gtest/gtest.h>
#include <Runtime/Object/Component.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Transform.h>
#include <vector>

namespace
{
    using NS::Object::Component;
    using NS::Object::GameObject;

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
    // GameObject が先に transform を積むので、 既定 priority の後入れは末尾に来る
    ASSERT_EQ(obj.Components().size(), std::size_t{2});
    EXPECT_EQ(obj.Components().back(), &comp);
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

TEST(GameObjectTest, DestroyFlipsIsAlive)
{
    GameObject obj;
    EXPECT_TRUE(obj.IsAlive());
    obj.Destroy();
    EXPECT_FALSE(obj.IsAlive());
}

namespace
{
    class HighPrioComponent : public NS::Object::Component
    {
    public:
        HighPrioComponent() noexcept : Component(NS::Object::TickPriority::EarlyUpdate) {}
    };

    class LowPrioComponent : public NS::Object::Component
    {
    public:
        LowPrioComponent() noexcept : Component(NS::Object::TickPriority::LateUpdate) {}
    };
} // namespace

TEST(GameObjectPriorityTest, AddComponentSortsByPriority)
{
    NS::Object::GameObject obj;
    auto& low = *obj.AddComponent<LowPrioComponent>();   // 先に追加 (LateUpdate, 400)
    auto& high = *obj.AddComponent<HighPrioComponent>(); // 後に追加 (Input, 0)

    // Input 0 の high、 GameObject が積む transform (Update 200)、 LateUpdate 400 の low の順
    ASSERT_EQ(obj.Components().size(), std::size_t{3});
    EXPECT_EQ(obj.Components()[0], &high);
    EXPECT_EQ(obj.Components()[2], &low);
}

TEST(GameObjectPriorityTest, SamePriorityPreservesInsertionOrder)
{
    NS::Object::GameObject obj;
    auto& a = *obj.AddComponent<HighPrioComponent>();
    auto& b = *obj.AddComponent<HighPrioComponent>();

    // Input 0 の 2 つが登録順のまま先頭に並び、 GameObject が積む transform (Update 200) は後ろ
    ASSERT_EQ(obj.Components().size(), std::size_t{3});
    EXPECT_EQ(obj.Components()[0], &a);
    EXPECT_EQ(obj.Components()[1], &b);
}

TEST(GameObjectAddComponentTest, OwnsLifetimeInjectsOwnerAndOrdersByPriority)
{
    NS::Object::GameObject obj;
    auto* low = obj.AddComponent<LowPrioComponent>();   // LateUpdate 400
    auto* high = obj.AddComponent<HighPrioComponent>(); // Input 0
    auto* mock = obj.AddComponent<MockComponent>();     // 既定 Update 200

    ASSERT_NE(low, nullptr);
    ASSERT_NE(high, nullptr);
    ASSERT_NE(mock, nullptr);
    EXPECT_EQ(low->Owner(), &obj);
    EXPECT_EQ(high->Owner(), &obj);

    // 寿命は GameObject が持つので、ローカル変数が無くても tick は伝わる
    obj.OnUpdate();
    EXPECT_EQ(mock->updateCount, 1);

    // priority 昇順 (Input 0 < Update 200 < LateUpdate 400)。 200 帯は GameObject が積む transform が先
    ASSERT_EQ(obj.Components().size(), std::size_t{4});
    EXPECT_EQ(obj.Components()[0], high);
    EXPECT_EQ(obj.Components()[2], mock);
    EXPECT_EQ(obj.Components()[3], low);
}

TEST(GameObjectTest, OwnerFlagGatesItsComponents)
{
    GameObject obj;
    auto* comp = obj.AddComponent<MockComponent>();
    ASSERT_NE(comp, nullptr);
    EXPECT_TRUE(comp->IsActive());

    obj.SetActive(false);
    EXPECT_FALSE(comp->IsActive());
    // component 自身の active は触られないので、owner を戻せばそのまま効く
    EXPECT_TRUE(comp->IsActiveSelf());

    obj.SetActive(true);
    EXPECT_TRUE(comp->IsActive());
}

TEST(GameObjectTest, AncestorFlagGatesDescendantComponents)
{
    GameObject root;
    GameObject child;
    GameObject grandChild;
    child.SetParent(&root);
    grandChild.SetParent(&child);
    auto* comp = grandChild.AddComponent<MockComponent>();
    ASSERT_NE(comp, nullptr);

    root.SetActive(false);
    EXPECT_FALSE(grandChild.IsActiveInHierarchy());
    EXPECT_TRUE(grandChild.IsActiveSelf());
    EXPECT_FALSE(comp->IsActive());

    root.SetActive(true);
    EXPECT_TRUE(comp->IsActive());
}

TEST(GameObjectTest, ChildKeepsItsOwnFlagWhileParentIsOff)
{
    GameObject parent;
    GameObject child;
    child.SetParent(&parent);
    child.SetActive(false);

    parent.SetActive(false);
    parent.SetActive(true);
    // 親を戻しても、自分で切った子は切れたまま
    EXPECT_FALSE(child.IsActiveInHierarchy());
}

TEST(GameObjectTest, InactiveOwnerSkipsUpdate)
{
    GameObject obj;
    auto* comp = obj.AddComponent<MockComponent>();
    ASSERT_NE(comp, nullptr);

    obj.SetActive(false);
    obj.OnUpdate();
    EXPECT_EQ(comp->updateCount, 0);

    obj.SetActive(true);
    obj.OnUpdate();
    EXPECT_EQ(comp->updateCount, 1);
}
