#include <gtest/gtest.h>

#include <Framework/Scene/Components/CharacterMovementComponent.h>
#include <Framework/Scene/Components/PlayerInputComponent.h>
#include <Framework/Scene/GameObject.h>

namespace
{
    using NS::Scene::CharacterMovementComponent;
    using NS::Scene::GameObject;
    using NS::Scene::PlayerInputComponent;
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
