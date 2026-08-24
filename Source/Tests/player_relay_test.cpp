#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerInputRelayComponent.h>
#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/PlayerInputComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Platform/Input.h>
#include <Runtime/Platform/Keyboard.h>
#include <gtest/gtest.h>

namespace
{
    using NS::Core::Vector3;
    using NS::Game::Player::PlayerComponent;
    using NS::Game::Player::PlayerInputRelayComponent;
    using NS::Object::GameObject;
    using NS::Platform::Key;

    constexpr float k_FixedDt = 1.0f / 60.0f;

    void BuildRelayRig(GameObject& owner)
    {
        owner.AddComponent<NS::Object::PlayerInputComponent>();
        owner.AddComponent<PlayerInputRelayComponent>();
        auto& player = *owner.AddComponent<PlayerComponent>();
        player.SetDebugDrawEnabled(false);
        owner.OnStart();
    }

    NS::Object::PlayerInputComponent& Input(GameObject& owner)
    {
        return *owner.FindComponent<NS::Object::PlayerInputComponent>();
    }

    PlayerInputRelayComponent& Relay(GameObject& owner)
    {
        return *owner.FindComponent<PlayerInputRelayComponent>();
    }

    PlayerComponent& Player(GameObject& owner)
    {
        return *owner.FindComponent<PlayerComponent>();
    }
} // namespace

class PlayerRelayTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        NS::Core::FrameTimer::SetFixedDelta(k_FixedDt);
        ClearKeyboard();
    }
    void TearDown() override { ClearKeyboard(); }

    //! 入力はプロセスに 1 個しか無い。押したキーを次のテストへ持ち越さないよう前後で払う
    static void ClearKeyboard() noexcept
    {
        auto& kb = NS::Platform::Input::Get().Keyboard();
        kb.ClearState();
        kb.Update();
    }

    //! 押しっぱなしのまま歩を 1 つ進める。押した瞬間の判定はここで消える
    static void AdvanceFrame() noexcept { NS::Platform::Input::Get().Keyboard().Update(); }
};

TEST_F(PlayerRelayTest, DirectionAndSpeedScaleReachThePlayerInTheSameStep)
{
    GameObject owner;
    BuildRelayRig(owner);
    Input(owner).SetCameraForward({0.0f, 0.0f, 1.0f});

    NS::Platform::Input::Get().Keyboard().OnKeyDown(Key::W);
    Input(owner).OnUpdate();
    Relay(owner).OnUpdate();

    EXPECT_GT(Player(owner).DesiredDirection().z, 0.9f);
    EXPECT_NEAR(Player(owner).DesiredDirection().x, 0.0f, 1.0e-5f);
    EXPECT_FLOAT_EQ(Player(owner).DesiredSpeedScale(), Input(owner).DesiredSpeedScale());
}

TEST_F(PlayerRelayTest, ClimbInputReachesThePlayer)
{
    GameObject owner;
    BuildRelayRig(owner);
    Input(owner).SetCameraForward({1.0f, 0.0f, 0.0f});

    NS::Platform::Input::Get().Keyboard().OnKeyDown(Key::D);
    Input(owner).OnUpdate();
    Relay(owner).OnUpdate();

    EXPECT_FLOAT_EQ(Player(owner).ClimbRight(), 1.0f);
    EXPECT_FLOAT_EQ(Player(owner).ClimbForward(), 0.0f);
}

TEST_F(PlayerRelayTest, JumpPressReachesThePlayerOnTheStepOfThePress)
{
    GameObject owner;
    BuildRelayRig(owner);

    NS::Platform::Input::Get().Keyboard().OnKeyDown(Key::Space);
    Input(owner).OnUpdate();
    Relay(owner).OnUpdate();

    Player(owner).SetGrounded(true);
    Player(owner).Jump(k_FixedDt);

    EXPECT_FLOAT_EQ(Player(owner).VerticalVelocity(), 12.0f);
}

// 押しっぱなしの歩まで押下を渡すと、押していない歩に跳ぶ
TEST_F(PlayerRelayTest, HoldOnlyStepDoesNotPressTheJump)
{
    GameObject owner;
    BuildRelayRig(owner);

    NS::Platform::Input::Get().Keyboard().OnKeyDown(Key::Space);
    Input(owner).OnUpdate();
    AdvanceFrame();
    Input(owner).OnUpdate();
    ASSERT_FALSE(Input(owner).JumpPressed());

    Relay(owner).OnUpdate();
    Player(owner).SetGrounded(true);
    Player(owner).Jump(k_FixedDt);

    EXPECT_FLOAT_EQ(Player(owner).VerticalVelocity(), 0.0f);
    EXPECT_EQ(Player(owner).JumpsRemaining(), 1);
}

TEST_F(PlayerRelayTest, JumpHoldStateIsRelayed)
{
    GameObject owner;
    BuildRelayRig(owner);

    auto& kb = NS::Platform::Input::Get().Keyboard();
    kb.OnKeyDown(Key::Space);
    Input(owner).OnUpdate();
    Relay(owner).OnUpdate();
    Player(owner).OnUpdate();

    kb.OnKeyUp(Key::Space);
    AdvanceFrame();
    Input(owner).OnUpdate();
    Relay(owner).OnUpdate();

    // 離した歩が届いていれば上昇が縮む
    Player(owner).SetVelocity(Vector3{0.0f, 10.0f, 0.0f});
    Player(owner).CutJumpRelease();

    EXPECT_FLOAT_EQ(Player(owner).VerticalVelocity(), 6.0f);
}

TEST_F(PlayerRelayTest, InactiveInputRelaysNothing)
{
    GameObject owner;
    BuildRelayRig(owner);
    Input(owner).SetCameraForward({0.0f, 0.0f, 1.0f});

    Player(owner).SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.5f);
    Input(owner).SetActive(false);

    NS::Platform::Input::Get().Keyboard().OnKeyDown(Key::W);
    Input(owner).OnUpdate();
    Relay(owner).OnUpdate();

    EXPECT_FLOAT_EQ(Player(owner).DesiredSpeedScale(), 0.5f);
    EXPECT_FLOAT_EQ(Player(owner).DesiredDirection().x, 1.0f);
}

TEST_F(PlayerRelayTest, MissingSideIsHarmless)
{
    GameObject withoutPlayer;
    withoutPlayer.AddComponent<NS::Object::PlayerInputComponent>();
    withoutPlayer.AddComponent<PlayerInputRelayComponent>();
    withoutPlayer.OnStart();
    Relay(withoutPlayer).OnUpdate();

    GameObject withoutInput;
    withoutInput.AddComponent<PlayerInputRelayComponent>();
    auto& player = *withoutInput.AddComponent<PlayerComponent>();
    player.SetDebugDrawEnabled(false);
    withoutInput.OnStart();

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.5f);
    Relay(withoutInput).OnUpdate();

    EXPECT_FLOAT_EQ(player.DesiredSpeedScale(), 0.5f);
}
