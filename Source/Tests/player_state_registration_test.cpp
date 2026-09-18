#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerState.h>
#include <Game/Player/States/BodySlamPlayerState.h>
#include <Game/Player/States/BrakePlayerState.h>
#include <Game/Player/States/FallPlayerState.h>
#include <Game/Player/States/IdlePlayerState.h>
#include <Game/Player/States/LedgeClimbingPlayerState.h>
#include <Game/Player/States/LedgeHangingPlayerState.h>
#include <Game/Player/States/WalkPlayerState.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/StateMachine.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace
{
    using PlayerComponent = NS::Game::Player::PlayerComponent;
    using Registry = NS::Object::StateRegistry<PlayerComponent>;
    using BodySlamPlayerState = NS::Game::Player::BodySlamPlayerState;
    using BrakePlayerState = NS::Game::Player::BrakePlayerState;
    using FallPlayerState = NS::Game::Player::FallPlayerState;
    using IdlePlayerState = NS::Game::Player::IdlePlayerState;
    using LedgeClimbingPlayerState = NS::Game::Player::LedgeClimbingPlayerState;
    using LedgeHangingPlayerState = NS::Game::Player::LedgeHangingPlayerState;
    using WalkPlayerState = NS::Game::Player::WalkPlayerState;

    // 状態の型を名指しする所が読むのはヘッダの k_Name だけで、状態の翻訳単位はどこからも参照されない
    // 参照されない翻訳単位の静的初期化はリンカに落とされ得るので、登録簿から作れることをここで見張る
    constexpr const char* k_MissingHint =
        " が登録簿にない。状態の翻訳単位がリンクされていない疑い。premake5.lua の project \"Tests\" の"
        " files に Source/Game/Player/**.cpp があるか、足した後に Tools\\@regen_project.cmd を掛けたかを見る";

    constexpr const char* k_StaleHint =
        " が登録簿にまだある。改名前の綴りが生きていると、シーン JSON の状態一覧を書き換え忘れても"
        " 状態機械が組めてしまい、書き換え漏れに気づけない";

    void ExpectRegistered(const char* name)
    {
        const std::unique_ptr<NS::Object::State<PlayerComponent>> state = Registry::Create(name);

        ASSERT_NE(state, nullptr) << name << k_MissingHint;
        EXPECT_STREQ(state->Name(), name);
    }

    void ExpectGone(const char* name)
    {
        EXPECT_EQ(Registry::Create(name), nullptr) << name << k_StaleHint;
    }
} // namespace

TEST(PlayerStateRegistrationTest, IdleIsCreatableByName)
{
    ExpectRegistered(IdlePlayerState::k_Name);
}

TEST(PlayerStateRegistrationTest, WalkIsCreatableByName)
{
    ExpectRegistered(WalkPlayerState::k_Name);
}

TEST(PlayerStateRegistrationTest, FallIsCreatableByName)
{
    ExpectRegistered(FallPlayerState::k_Name);
}

TEST(PlayerStateRegistrationTest, LedgeHangingIsCreatableByName)
{
    ExpectRegistered(LedgeHangingPlayerState::k_Name);
}

TEST(PlayerStateRegistrationTest, LedgeClimbingIsCreatableByName)
{
    ExpectRegistered(LedgeClimbingPlayerState::k_Name);
}

TEST(PlayerStateRegistrationTest, BodySlamIsCreatableByName)
{
    ExpectRegistered(BodySlamPlayerState::k_Name);
}

TEST(PlayerStateRegistrationTest, BrakeIsCreatableByName)
{
    ExpectRegistered(BrakePlayerState::k_Name);
}

TEST(PlayerStateRegistrationTest, UnknownNameCreatesNothing)
{
    EXPECT_EQ(Registry::Create("登録していない状態"), nullptr);
}

TEST(PlayerStateRegistrationTest, RenamedStatesAreGone)
{
    ExpectGone("Locomotion");
    ExpectGone("LedgeHang");
    ExpectGone("LedgeMantle");
}

TEST(PlayerStateRegistrationTest, SevenStatesBuildAMachineStartingAtIdle)
{
    NS::Object::GameObject obj;
    PlayerComponent& player = *obj.AddComponent<PlayerComponent>();

    const std::vector<std::string> names{
        IdlePlayerState::k_Name,
        WalkPlayerState::k_Name,
        FallPlayerState::k_Name,
        LedgeHangingPlayerState::k_Name,
        LedgeClimbingPlayerState::k_Name,
        BodySlamPlayerState::k_Name,
        BrakePlayerState::k_Name,
    };

    NS::Object::StateMachine<PlayerComponent> machine;

    EXPECT_TRUE(machine.Build(player, names));
    EXPECT_STREQ(machine.CurrentName(), IdlePlayerState::k_Name);
}
