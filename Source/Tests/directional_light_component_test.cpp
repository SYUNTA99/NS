#include <Runtime/Object/Component.h>
#include <Runtime/Object/Components/DirectionalLightComponent.h>
#include <Runtime/Object/GameObject.h>
#include <gtest/gtest.h>

namespace
{
    using NS::Object::Component;
    using NS::Object::ComponentCast;
    using NS::Object::DirectionalLightComponent;
    using NS::Object::GameObject;
} // namespace

TEST(DirectionalLightComponentTest, DefaultsAreNeutralSun)
{
    DirectionalLightComponent light;
    EXPECT_FLOAT_EQ(light.Direction().x, -0.3f);
    EXPECT_FLOAT_EQ(light.Direction().y, -1.0f);
    EXPECT_FLOAT_EQ(light.Direction().z, -0.2f);
    EXPECT_FLOAT_EQ(light.Color().x, 1.0f);
    EXPECT_FLOAT_EQ(light.Color().y, 1.0f);
    EXPECT_FLOAT_EQ(light.Color().z, 1.0f);
    EXPECT_FLOAT_EQ(light.Ambient().x, 0.30f);
    EXPECT_FLOAT_EQ(light.Ambient().y, 0.34f);
    EXPECT_FLOAT_EQ(light.Ambient().z, 0.40f);
    EXPECT_FLOAT_EQ(light.Ground().x, 0.24f);
    EXPECT_FLOAT_EQ(light.Ground().y, 0.21f);
    EXPECT_FLOAT_EQ(light.Ground().z, 0.18f);
    EXPECT_FLOAT_EQ(light.Exposure(), 1.35f);
}

// 空側と地面側が同じ色だと単色の環境光に戻り、 影の中で面の向きが読めなくなる
TEST(DirectionalLightComponentTest, SkyAmbientIsBrighterThanGround)
{
    DirectionalLightComponent light;
    EXPECT_GT(light.Ambient().z, light.Ground().z);
}

TEST(DirectionalLightComponentTest, ClassNameIsRegistered)
{
    DirectionalLightComponent light;
    EXPECT_STREQ(light.ClassName(), "DirectionalLightComponent");
}

TEST(DirectionalLightComponentTest, ComponentCastFindsItThroughBase)
{
    // ForEachComponent は ComponentCast で拾うので、基底越しに引ければ集約でも拾える
    GameObject obj;
    Component* base = obj.AddComponent<DirectionalLightComponent>();
    ASSERT_NE(base, nullptr);
    EXPECT_NE(ComponentCast<DirectionalLightComponent>(base), nullptr);
}
