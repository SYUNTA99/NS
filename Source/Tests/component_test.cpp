#include <Runtime/Object/Component.h>
#include <Runtime/Object/GameObject.h>
#include <gtest/gtest.h>

namespace
{
    using NS::Object::Component;
    using NS::Object::GameObject;

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

TEST(ComponentTest, SetEnabledStopsUpdate)
{
    GameObject obj;
    auto& c = *obj.AddComponent<CountingComponent>();

    c.SetEnabled(false);
    obj.OnUpdate();
    EXPECT_EQ(c.updateCount, 0);

    c.SetEnabled(true);
    obj.OnUpdate();
    EXPECT_EQ(c.updateCount, 1);
}

TEST(ComponentTest, EnabledAndActiveAreSeparateFlags)
{
    CountingComponent c;

    // モード切替の休止はデータの active を触らない
    c.SetActive(false);
    EXPECT_TRUE(c.IsEnabled());
    EXPECT_FALSE(c.IsActive());

    // データの active を切った側は、 休止を解いても効かないまま
    c.SetEnabled(false);
    c.SetActive(true);
    EXPECT_TRUE(c.IsActiveSelf());
    EXPECT_FALSE(c.IsActive());
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
    class LifetimeComponent : public NS::Object::Component
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
    NS::Object::GameObject obj;
    auto* c = obj.AddComponent<CountingComponent>();

    // GameObject が先に transform を積むので、 同 priority の後入れは末尾に来る
    ASSERT_EQ(obj.Components().size(), std::size_t{2});
    EXPECT_EQ(obj.Components().back(), c);
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
        NS::Object::GameObject obj;
        obj.AddComponent<LifetimeComponent>(&destroyed);
        EXPECT_EQ(obj.Components().size(), std::size_t{2});
        EXPECT_EQ(destroyed, 0);
    }
    EXPECT_EQ(destroyed, 1); // GameObject 破棄で所有 Component も破棄される
}
