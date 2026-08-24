#include <Game/Level/AreaCameraActivatorComponent.h>
#include <Game/Level/FinisherComponent.h>
#include <Game/Level/FollowCameraFeedComponent.h>
#include <Game/Level/HealthComponent.h>
#include <Game/Level/RespawnerComponent.h>
#include <Game/Level/ScreenFadeComponent.h>
#include <Game/Player.h>
#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerInputRelayComponent.h>
#include <Game/Player/PlayerStateManagerComponent.h>
#include <Game/Player/PlayerStatsManagerComponent.h>
#include <Runtime/Object/Components/MeshRendererComponent.h>
#include <Runtime/Object/Components/PlayerInputComponent.h>
#include <Runtime/Object/Components/ShadowComponent.h>
#include <Runtime/Object/Components/TransformComponent.h>
#include <Runtime/Platform/Input.h>
#include <Runtime/Platform/Keyboard.h>
#include <gtest/gtest.h>

TEST(PlayerTest, ConstructsWithDefaultComposition)
{
    Player player{};
    std::size_t expected = 14u;
#if !defined(NS_SHIPPING)
    expected += 1u;
#endif
    EXPECT_EQ(player.Components().size(), expected);
}

TEST(PlayerTest, DefaultComponentsResolveByType)
{
    Player player{};
    EXPECT_EQ(player.FindComponent<NS::Object::PlayerInputComponent>(), player.Components()[0]);
    EXPECT_EQ(player.FindComponent<NS::Game::Player::PlayerInputRelayComponent>(), player.Components()[1]);
    EXPECT_EQ(player.FindComponent<NS::Object::TransformComponent>(), player.Components()[2]);
    EXPECT_EQ(player.FindComponent<NS::Object::MeshRendererComponent>(), player.Components()[3]);
    EXPECT_EQ(player.FindComponent<NS::Game::Player::PlayerStatsManagerComponent>(), player.Components()[4]);
    EXPECT_EQ(player.FindComponent<NS::Game::Player::PlayerStateManagerComponent>(), player.Components()[5]);
    EXPECT_EQ(player.FindComponent<NS::Game::Player::PlayerComponent>(), player.Components()[6]);
    EXPECT_EQ(player.FindComponent<NS::Game::Level::HealthComponent>(), player.Components()[7]);
    EXPECT_EQ(player.FindComponent<NS::Object::ShadowComponent>(), player.Components()[8]);
}

TEST(PlayerTest, ResponseComponentsTrailTheUpdateBand)
{
    Player player{};
    const std::size_t count = player.Components().size();
    EXPECT_EQ(player.FindComponent<NS::Game::Level::ScreenFadeComponent>(), player.Components()[count - 5]);
    EXPECT_EQ(player.FindComponent<NS::Game::Level::RespawnerComponent>(), player.Components()[count - 4]);
    EXPECT_EQ(player.FindComponent<NS::Game::Level::FinisherComponent>(), player.Components()[count - 3]);
    EXPECT_EQ(player.FindComponent<NS::Game::Level::AreaCameraActivatorComponent>(), player.Components()[count - 2]);
    EXPECT_EQ(player.FindComponent<NS::Game::Level::FollowCameraFeedComponent>(), player.Components()[count - 1]);
}

TEST(PlayerTest, InputRelayResolvesBothSidesOnStart)
{
    auto& keyboard = NS::Platform::Input::Get().Keyboard();
    keyboard.ClearState();
    keyboard.Update();

    Player player{};
    player.OnStart();

    auto* input = player.FindComponent<NS::Object::PlayerInputComponent>();
    auto* relay = player.FindComponent<NS::Game::Player::PlayerInputRelayComponent>();
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
