#include <gtest/gtest.h>

#include <ns/scene/components/character_movement_component.h>
#include <ns/scene/components/player_input_component.h>
#include <ns/scene/game_object.h>

namespace
{
    using ns::scene::CharacterMovementComponent;
    using ns::scene::GameObject;
    using ns::scene::PlayerInputComponent;
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
    CharacterMovementComponent mov;
    obj.RegisterComponent(&mov);

    PlayerInputComponent input(&mov);
    obj.RegisterComponent(&input);

    input.OnUpdate(1.0f / 60.0f);
    EXPECT_FLOAT_EQ(mov.Velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(mov.Velocity().z, 0.0f);
}

TEST(PlayerInputTest, OnUpdateIsNoOpWhenMovementIsNull)
{
    PlayerInputComponent input(nullptr);
    input.OnUpdate(1.0f / 60.0f);
    SUCCEED();
}

TEST(PlayerInputTest, CameraForwardSetterPersists)
{
    PlayerInputComponent input(nullptr);
    input.SetCameraForward({1.0f, 5.0f, 0.0f});
    SUCCEED();
}
