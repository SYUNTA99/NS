#include <gtest/gtest.h>

#include <ns/scene/component.h>
#include <ns/scene/game_object.h>

namespace
{
    using ns::scene::Component;
    using ns::scene::GameObject;

    class CountingComponent : public Component
    {
    public:
        int updateCount = 0;
        void OnUpdate(float) override { ++updateCount; }
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
    CountingComponent c;
    obj.RegisterComponent(&c);

    c.SetActive(false);
    obj.OnUpdate(0.016f);
    EXPECT_EQ(c.updateCount, 0);

    c.SetActive(true);
    obj.OnUpdate(0.016f);
    EXPECT_EQ(c.updateCount, 1);
}

TEST(ComponentTest, RootTransformReturnsOwnerRoot)
{
    GameObject obj;
    CountingComponent c;
    obj.RegisterComponent(&c);

    obj.Root().SetPosition({1.0f, 2.0f, 3.0f});

    EXPECT_FLOAT_EQ(c.RootTransform().Position().x, 1.0f);
    EXPECT_FLOAT_EQ(c.RootTransform().Position().y, 2.0f);
    EXPECT_FLOAT_EQ(c.RootTransform().Position().z, 3.0f);
}
