#include <Game/Level/AreaCameraActivator.h>
#include <Game/Level/Finisher.h>
#include <Game/Level/Health.h>
#include <Game/Level/Respawner.h>
#include <Game/Level/ScreenFade.h>
#include <Game/Player.h>
#include <Game/Player/PlayerCameraSpawner.h>
#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerInputRelay.h>
#include <Game/Player/PlayerStateManager.h>
#include <Runtime/Object/Components/MeshRenderer.h>
#include <Runtime/Object/Components/PlayerInput.h>
#include <Runtime/Object/Components/Shadow.h>
#include <Runtime/Object/Components/TransformComponent.h>
#include <Runtime/Platform/Input.h>
#include <Runtime/Platform/Keyboard.h>
#include <gtest/gtest.h>

TEST(PlayerTest, ConstructsWithDefaultComposition)
{
    Player player{};
    EXPECT_EQ(player.Components().size(), 13u);
}

TEST(PlayerTest, DefaultComponentsResolveByType)
{
    Player player{};
    EXPECT_EQ(player.FindComponent<NS::Obj::PlayerInput>(), player.Components()[0]);
    EXPECT_EQ(player.FindComponent<NS::Game::Player::PlayerInputRelay>(), player.Components()[1]);
    EXPECT_EQ(player.FindComponent<NS::Obj::TransformComponent>(), player.Components()[2]);
    EXPECT_EQ(player.FindComponent<NS::Obj::MeshRenderer>(), player.Components()[3]);
    EXPECT_EQ(player.FindComponent<NS::Game::Player::PlayerStateManager>(), player.Components()[4]);
    EXPECT_EQ(player.FindComponent<NS::Game::Player::PlayerComponent>(), player.Components()[5]);
    EXPECT_EQ(player.FindComponent<NS::Game::Level::Health>(), player.Components()[6]);
    EXPECT_EQ(player.FindComponent<NS::Obj::Shadow>(), player.Components()[7]);
}

TEST(PlayerTest, ResponseComponentsTrailTheUpdateBand)
{
    Player player{};
    const std::size_t count = player.Components().size();
    EXPECT_EQ(player.FindComponent<NS::Game::Level::ScreenFade>(), player.Components()[count - 5]);
    EXPECT_EQ(player.FindComponent<NS::Game::Level::Respawner>(), player.Components()[count - 4]);
    EXPECT_EQ(player.FindComponent<NS::Game::Level::Finisher>(), player.Components()[count - 3]);
    EXPECT_EQ(player.FindComponent<NS::Game::Level::AreaCameraActivator>(), player.Components()[count - 2]);
    EXPECT_EQ(player.FindComponent<NS::Game::Player::PlayerCameraSpawner>(), player.Components()[count - 1]);
}

TEST(PlayerTest, InputRelayResolvesBothSidesOnStart)
{
    auto& keyboard = NS::Platform::Input::Get().Keyboard();
    keyboard.ClearState();
    keyboard.Update();

    Player player{};
    player.OnStart();

    auto* input = player.FindComponent<NS::Obj::PlayerInput>();
    auto* relay = player.FindComponent<NS::Game::Player::PlayerInputRelay>();
    auto* entity = player.FindComponent<NS::Game::Player::PlayerComponent>();
    ASSERT_NE(input, nullptr);
    ASSERT_NE(relay, nullptr);
    ASSERT_NE(entity, nullptr);

    input->SetCameraForward({0.0f, 0.0f, 1.0f});
    keyboard.OnKeyDown(NS::Platform::Key::W);
    input->OnUpdate();
    relay->OnUpdate();

    EXPECT_GT(entity->DesiredSpeedScale(), 0.0f);
    EXPECT_GT(entity->DesiredDirection().z, 0.9f);

    keyboard.ClearState();
    keyboard.Update();
}
