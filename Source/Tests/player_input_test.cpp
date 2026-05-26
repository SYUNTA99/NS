#include <gtest/gtest.h>

#include <Framework/Scene/CharacterMovementComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/PlayerInputComponent.h>

namespace
{
    using NS::Scene::CharacterMovementComponent;
    using NS::Scene::GameObject;
    using NS::Scene::PlayerInputComponent;
} // namespace

TEST(PlayerInputTest, ConstructsWithNullMovementWithoutCrashing)
{
    PlayerInputComponent input(nullptr, nullptr);
    EXPECT_EQ(input.Movement(), nullptr);
    EXPECT_TRUE(input.IsActive());
}

TEST(PlayerInputTest, OnUpdateIsNoOpWhenInputIsNull)
{
    GameObject obj;
    CharacterMovementComponent mov(&obj);

    PlayerInputComponent input(&obj, &mov);

    input.OnUpdate();
    EXPECT_FLOAT_EQ(mov.Velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(mov.Velocity().z, 0.0f);
}

TEST(PlayerInputTest, OnUpdateIsNoOpWhenMovementIsNull)
{
    PlayerInputComponent input(nullptr, nullptr);
    input.OnUpdate();
    SUCCEED();
}

TEST(PlayerInputTest, CameraForwardSetterPersists)
{
    PlayerInputComponent input(nullptr, nullptr);
    input.SetCameraForward({1.0f, 5.0f, 0.0f});
    SUCCEED();
}
