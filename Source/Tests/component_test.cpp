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
    CountingComponent c(nullptr);
    EXPECT_TRUE(c.IsActive());
}

TEST(ComponentTest, SetActiveTogglesPropagation)
{
    GameObject obj;
    CountingComponent c(&obj);

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
    CountingComponent c(&obj);

    obj.Root().SetPosition({1.0f, 2.0f, 3.0f});

    EXPECT_FLOAT_EQ(c.RootTransform().Position().x, 1.0f);
    EXPECT_FLOAT_EQ(c.RootTransform().Position().y, 2.0f);
    EXPECT_FLOAT_EQ(c.RootTransform().Position().z, 3.0f);
}

namespace
{
    class AutoRegComponent : public NS::Scene::Component
    {
    public:
        using NS::Scene::Component::Component;
    };
} // namespace

TEST(ComponentAutoRegisterTest, CtorWithOwnerAutoRegisters)
{
    NS::Scene::GameObject obj;
    AutoRegComponent c(&obj);

    ASSERT_EQ(obj.Components().size(), std::size_t{1});
    EXPECT_EQ(obj.Components()[0], &c);
    EXPECT_EQ(c.Owner(), &obj);
}

TEST(ComponentAutoRegisterTest, CtorWithNullOwnerDoesNotRegister)
{
    AutoRegComponent c(nullptr);
    EXPECT_EQ(c.Owner(), nullptr);
}

TEST(ComponentAutoRegisterTest, DtorAutoUnregisters)
{
    NS::Scene::GameObject obj;
    {
        AutoRegComponent c(&obj);
        EXPECT_EQ(obj.Components().size(), std::size_t{1});
    }
    EXPECT_EQ(obj.Components().size(), std::size_t{0});
}
