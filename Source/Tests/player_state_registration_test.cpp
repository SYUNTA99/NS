#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerState.h>
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

    // 状態を名指しで呼ぶコードはどこにも無い。参照されない翻訳単位の静的初期化はリンカに落とされ得るので、
    // 登録簿から作れることをここで見張る
    constexpr const char* k_MissingHint =
        " が登録簿にない。状態の翻訳単位がリンクされていない疑い。premake5.lua の project \"Tests\" の"
        " files に Source/Game/Player/**.cpp があるか、足した後に tools\\@regen_project.cmd を掛けたかを見る";

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

// 掴まりと突進が戻る先の定数をそのまま登録簿へ通し、綴りがずれていないことも同時に見る
TEST(PlayerStateRegistrationTest, IdleIsCreatableByName)
{
    ExpectRegistered(PlayerComponent::k_IdleStateName);
}

TEST(PlayerStateRegistrationTest, WalkIsCreatableByName)
{
    ExpectRegistered("Walk");
}

TEST(PlayerStateRegistrationTest, FallIsCreatableByName)
{
    ExpectRegistered("Fall");
}

TEST(PlayerStateRegistrationTest, LedgeHangingIsCreatableByName)
{
    ExpectRegistered(PlayerComponent::k_LedgeHangingStateName);
}

TEST(PlayerStateRegistrationTest, LedgeClimbingIsCreatableByName)
{
    ExpectRegistered(PlayerComponent::k_LedgeClimbingStateName);
}

TEST(PlayerStateRegistrationTest, BodySlamIsCreatableByName)
{
    ExpectRegistered(PlayerComponent::k_BodySlamStateName);
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

TEST(PlayerStateRegistrationTest, SixStatesBuildAMachineStartingAtIdle)
{
    NS::Object::GameObject obj;
    PlayerComponent& player = *obj.AddComponent<PlayerComponent>();

    const std::vector<std::string> names{
        PlayerComponent::k_IdleStateName,
        "Walk",
        "Fall",
        PlayerComponent::k_LedgeHangingStateName,
        PlayerComponent::k_LedgeClimbingStateName,
        PlayerComponent::k_BodySlamStateName,
    };

    NS::Object::StateMachine<PlayerComponent> machine;

    EXPECT_TRUE(machine.Build(player, names));
    EXPECT_STREQ(machine.CurrentName(), PlayerComponent::k_IdleStateName);
}
