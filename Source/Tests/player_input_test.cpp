#include <gtest/gtest.h>
#include <Runtime/Object/Components/CharacterMovementComponent.h>
#include <Runtime/Object/Components/PlayerInputComponent.h>
#include <Runtime/Object/GameObject.h>

namespace
{
    using NS::Object::CharacterMovementComponent;
    using NS::Object::GameObject;
    using NS::Object::PlayerInputComponent;
} // namespace

TEST(PlayerInputTest, ConstructsWithoutMovementResolved)
{
    PlayerInputComponent input;
    EXPECT_EQ(input.Movement(), nullptr);
    EXPECT_TRUE(input.IsActive());
}

TEST(PlayerInputTest, OnStartResolvesSiblingMovement)
{
    GameObject obj;
    auto& mov = *obj.AddComponent<CharacterMovementComponent>();
    auto& input = *obj.AddComponent<PlayerInputComponent>();

    obj.OnStart();

    EXPECT_EQ(input.Movement(), &mov);
}

TEST(PlayerInputTest, OnUpdateIsNoOpWhenMovementIsNull)
{
    PlayerInputComponent input;
    input.OnUpdate();
    SUCCEED();
}

TEST(PlayerInputTest, CameraForwardSetterPersists)
{
    PlayerInputComponent input;
    input.SetCameraForward({1.0f, 5.0f, 0.0f});
    SUCCEED();
}
