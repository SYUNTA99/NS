#include <gtest/gtest.h>

#include <Framework/Scene/Component.h>
#include <Framework/Scene/GameObject.h>

namespace
{
    using NS::Scene::Component;
    using NS::Scene::GameObject;

    class CountingComponent : public Component
    {
    public:
        using Component::Component;
        int updateCount = 0;
        void OnUpdate() override { ++updateCount; }
    };
} // namespace

TEST(ComponentTest, DefaultIsActive)
{
    CountingComponent c;
    EXPECT_TRUE(c.IsActive());
}

TEST(ComponentTest, SetActiveTogglesPropagation)
{
    GameObject obj;
    auto& c = *obj.AddComponent<CountingComponent>();

    c.SetActive(false);
    obj.OnUpdate();
    EXPECT_EQ(c.updateCount, 0);

    c.SetActive(true);
    obj.OnUpdate();
    EXPECT_EQ(c.updateCount, 1);
}

TEST(ComponentTest, RootTransformReturnsOwnerRoot)
{
    GameObject obj;
    auto& c = *obj.AddComponent<CountingComponent>();

    obj.Root().SetPosition({1.0f, 2.0f, 3.0f});

    EXPECT_FLOAT_EQ(c.RootTransform().Position().x, 1.0f);
    EXPECT_FLOAT_EQ(c.RootTransform().Position().y, 2.0f);
    EXPECT_FLOAT_EQ(c.RootTransform().Position().z, 3.0f);
}

namespace
{
    /// 破棄回数を外部カウンタへ記録する Component。 GameObject 所有の寿命検証に使う
    class LifetimeComponent : public NS::Scene::Component
    {
    public:
        explicit LifetimeComponent(int* destroyCounter) noexcept : m_destroyCounter(destroyCounter) {}
        ~LifetimeComponent() noexcept override
        {
            if (m_destroyCounter != nullptr)
                ++(*m_destroyCounter);
        }

    private:
        int* m_destroyCounter = nullptr;
    };
} // namespace

TEST(ComponentOwnershipTest, AddComponentRegistersAndInjectsOwner)
{
    NS::Scene::GameObject obj;
    auto* c = obj.AddComponent<CountingComponent>();

    ASSERT_EQ(obj.Components().size(), std::size_t{1});
    EXPECT_EQ(obj.Components()[0], c);
    EXPECT_EQ(c->Owner(), &obj);
}

TEST(ComponentOwnershipTest, DetachedComponentHasNullOwner)
{
    CountingComponent c;
    EXPECT_EQ(c.Owner(), nullptr);
}

TEST(ComponentOwnershipTest, GameObjectOwnsComponentLifetime)
{
    int destroyed = 0;
    {
        NS::Scene::GameObject obj;
        obj.AddComponent<LifetimeComponent>(&destroyed);
        EXPECT_EQ(obj.Components().size(), std::size_t{1});
        EXPECT_EQ(destroyed, 0);
    }
    EXPECT_EQ(destroyed, 1); // GameObject 破棄で所有 Component も破棄される
}
