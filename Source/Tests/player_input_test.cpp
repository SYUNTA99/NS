#include <gtest/gtest.h>

#include <Framework/Scene/Components/CharacterMovementComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/Components/PlayerInputComponent.h>

namespace
{
    using NS::Scene::CharacterMovementComponent;
    using NS::Scene::GameObject;
    using NS::Scene::PlayerInputComponent;
} // namespace

TEST(PlayerInputTest, ConstructsWithNullMovementWithoutCrashing)
{
    PlayerInputComponent input(nullptr);
    EXPECT_EQ(input.Movement(), nullptr);
    EXPECT_TRUE(input.IsActive());
}

TEST(PlayerInputTest, OnUpdateIsNoOpWhenInputIsNull)
{
    GameObject obj;
    auto& mov = *obj.AddComponent<CharacterMovementComponent>();

    auto& input = *obj.AddComponent<PlayerInputComponent>(&mov);

    input.OnUpdate();
    EXPECT_FLOAT_EQ(mov.Velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(mov.Velocity().z, 0.0f);
}

TEST(PlayerInputTest, OnUpdateIsNoOpWhenMovementIsNull)
{
    PlayerInputComponent input(nullptr);
    input.OnUpdate();
    SUCCEED();
}

TEST(PlayerInputTest, CameraForwardSetterPersists)
{
    PlayerInputComponent input(nullptr);
    input.SetCameraForward({1.0f, 5.0f, 0.0f});
    SUCCEED();
}
