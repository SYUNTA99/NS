#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerState.h>
#include <Runtime/Object/StateMachine.h>
#include <gtest/gtest.h>

#include <memory>

namespace
{
    using PlayerComponent = NS::Game::Player::PlayerComponent;
    using Registry = NS::Object::StateRegistry<PlayerComponent>;

    // 状態を名指しで呼ぶコードはどこにも無い。参照されない翻訳単位の静的初期化はリンカに落とされ得るので、
    // 登録簿から作れることをここで見張る
    constexpr const char* k_MissingHint =
        " が登録簿にない。状態の翻訳単位がリンクされていない疑い。premake5.lua の project \"Tests\" の"
        " files に Source/Game/Player/**.cpp があるか、足した後に tools\\@regen_project.cmd を掛けたかを見る";

    void ExpectRegistered(const char* name)
    {
        const std::unique_ptr<NS::Object::State<PlayerComponent>> state = Registry::Create(name);

        ASSERT_NE(state, nullptr) << name << k_MissingHint;
        EXPECT_STREQ(state->Name(), name);
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

TEST(PlayerStateRegistrationTest, UnknownNameCreatesNothing)
{
    EXPECT_EQ(Registry::Create("登録していない状態"), nullptr);
}
