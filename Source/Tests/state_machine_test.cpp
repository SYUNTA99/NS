#include "Runtime/Object/StateMachine.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

/// 機構単体の検証。所有者は呼ばれた OnEnter / OnExit を記録するだけで、状態はテスト内で登録する

namespace
{
    struct Rig
    {
        std::vector<std::string> log;
    };

    class AlphaState final : public NS::Object::State<Rig>
    {
    public:
        static constexpr const char* k_Name = "Alpha";
        [[nodiscard]] const char* Name() const noexcept override { return k_Name; }
        void OnEnter(Rig& rig) override { rig.log.push_back("A:enter"); }
        void OnStep(Rig& rig, float) override { rig.log.push_back("A:step"); }
        void OnExit(Rig& rig) override { rig.log.push_back("A:exit"); }
    };
    NS_STATE(AlphaState, Rig)

    class BetaState final : public NS::Object::State<Rig>
    {
    public:
        static constexpr const char* k_Name = "Beta";
        [[nodiscard]] const char* Name() const noexcept override { return k_Name; }
        void OnEnter(Rig& rig) override { rig.log.push_back("B:enter"); }
        void OnStep(Rig& rig, float) override { rig.log.push_back("B:step"); }
        void OnExit(Rig& rig) override { rig.log.push_back("B:exit"); }
    };
    NS_STATE(BetaState, Rig)
} // namespace

TEST(StateMachineTest, BuildEntersFirstState)
{
    Rig rig;
    NS::Object::StateMachine<Rig> machine;

    EXPECT_TRUE(machine.Build(rig, {"Alpha", "Beta"}));
    EXPECT_TRUE(machine.IsBuilt());
    EXPECT_STREQ(machine.CurrentName(), "Alpha");
    EXPECT_EQ(rig.log, (std::vector<std::string>{"A:enter"}));

    machine.Step(rig, 0.016f);
    EXPECT_EQ(rig.log.back(), "A:step");
}

TEST(StateMachineTest, UnknownNamesAreSkippedAndReported)
{
    Rig rig;
    NS::Object::StateMachine<Rig> machine;

    // 未登録名は飛ばして残りで組む。全滅なら未組立のまま
    EXPECT_FALSE(machine.Build(rig, {"Alpha", "Nope"}));
    EXPECT_TRUE(machine.IsBuilt());
    EXPECT_STREQ(machine.CurrentName(), "Alpha");

    NS::Object::StateMachine<Rig> empty;
    EXPECT_FALSE(empty.Build(rig, {"Nope"}));
    EXPECT_FALSE(empty.IsBuilt());
    empty.Step(rig, 0.016f);
    EXPECT_STREQ(empty.CurrentName(), "");
}

TEST(StateMachineTest, ChangeRunsExitThenEnter)
{
    Rig rig;
    NS::Object::StateMachine<Rig> machine;
    machine.Build(rig, {"Alpha", "Beta"});
    rig.log.clear();

    EXPECT_TRUE(machine.Change(rig, "Beta"));
    EXPECT_STREQ(machine.CurrentName(), "Beta");
    EXPECT_EQ(rig.log, (std::vector<std::string>{"A:exit", "B:enter"}));

    machine.Step(rig, 0.016f);
    EXPECT_EQ(rig.log.back(), "B:step");
}

TEST(StateMachineTest, ChangeByTypeUsesRegisteredName)
{
    Rig rig;
    NS::Object::StateMachine<Rig> machine;
    machine.Build(rig, {"Alpha", "Beta"});

    EXPECT_TRUE(machine.Change<BetaState>(rig));
    EXPECT_STREQ(machine.CurrentName(), "Beta");
}

TEST(StateMachineTest, ChangeToUnknownOrSameDoesNothing)
{
    Rig rig;
    NS::Object::StateMachine<Rig> machine;
    machine.Build(rig, {"Alpha", "Beta"});
    rig.log.clear();

    // 未知名は現状維持で false。一覧に居ない登録名 (Beta を外した一覧) も同じ
    EXPECT_FALSE(machine.Change(rig, "Nope"));
    EXPECT_STREQ(machine.CurrentName(), "Alpha");

    // 同じ状態への Change は OnExit / OnEnter を呼び直さない
    EXPECT_TRUE(machine.Change(rig, "Alpha"));
    EXPECT_TRUE(rig.log.empty());
}

TEST(StateMachineTest, ResetReturnsToHeadWithoutCeremony)
{
    Rig rig;
    NS::Object::StateMachine<Rig> machine;
    machine.Build(rig, {"Alpha", "Beta"});
    machine.Change(rig, "Beta");
    rig.log.clear();

    machine.Reset();

    EXPECT_STREQ(machine.CurrentName(), "Alpha");
    EXPECT_TRUE(rig.log.empty());
}

TEST(StateMachineTest, ChangeOnlyReachesStatesInBuiltList)
{
    Rig rig;
    NS::Object::StateMachine<Rig> machine;
    // 一覧に Beta を入れなければ、登録済みでも遷移できない (データが持てる状態の全集合)
    machine.Build(rig, {"Alpha"});

    EXPECT_FALSE(machine.Change(rig, "Beta"));
    EXPECT_STREQ(machine.CurrentName(), "Alpha");
}
