#include "Runtime/Object/StateMachine.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

//! 機構単体の検証。所有者は呼ばれた OnEnter / OnStep / OnExit を記録するだけ

namespace
{
    struct Rig
    {
        std::vector<std::string> log;
    };

    class AlphaState final : public NS::Obj::StateOf<AlphaState, Rig>
    {
    public:
        static constexpr const char* k_Name = "Alpha";
        void OnEnter(Rig& rig) override { rig.log.push_back("A:enter"); }
        void OnStep(Rig& rig, float) override { rig.log.push_back("A:step"); }
        void OnExit(Rig& rig) override { rig.log.push_back("A:exit"); }
    };

    class BetaState final : public NS::Obj::StateOf<BetaState, Rig>
    {
    public:
        static constexpr const char* k_Name = "Beta";
        void OnEnter(Rig& rig) override { rig.log.push_back("B:enter"); }
        void OnStep(Rig& rig, float) override { rig.log.push_back("B:step"); }
        void OnExit(Rig& rig) override { rig.log.push_back("B:exit"); }
    };

    class GammaState final : public NS::Obj::StateOf<GammaState, Rig>
    {
    public:
        static constexpr const char* k_Name = "Gamma";
        void OnStep(Rig&, float) override {}
    };
} // namespace

TEST(StateMachineTest, BuildEntersFirstState)
{
    Rig rig;
    NS::Obj::StateMachine<Rig> machine;

    machine.Build<AlphaState, BetaState>(rig);

    EXPECT_TRUE(machine.IsBuilt());
    EXPECT_TRUE(machine.IsCurrent<AlphaState>());
    EXPECT_STREQ(machine.CurrentName(), "Alpha");
    EXPECT_EQ(rig.log, (std::vector<std::string>{"A:enter"}));

    machine.Step(rig, 0.016f);
    EXPECT_EQ(rig.log.back(), "A:step");
}

TEST(StateMachineTest, ChangeRunsExitThenEnter)
{
    Rig rig;
    NS::Obj::StateMachine<Rig> machine;
    machine.Build<AlphaState, BetaState>(rig);
    rig.log.clear();

    EXPECT_TRUE(machine.Change<BetaState>(rig));
    EXPECT_TRUE(machine.IsCurrent<BetaState>());
    EXPECT_EQ(rig.log, (std::vector<std::string>{"A:exit", "B:enter"}));

    machine.Step(rig, 0.016f);
    EXPECT_EQ(rig.log.back(), "B:step");
}

// 同じ状態への Change は OnExit / OnEnter を呼び直さない
TEST(StateMachineTest, ChangeToTheSameStateDoesNothing)
{
    Rig rig;
    NS::Obj::StateMachine<Rig> machine;
    machine.Build<AlphaState, BetaState>(rig);
    rig.log.clear();

    EXPECT_TRUE(machine.Change<AlphaState>(rig));

    EXPECT_TRUE(rig.log.empty());
}

TEST(StateMachineTest, ResetReturnsToHeadWithoutCeremony)
{
    Rig rig;
    NS::Obj::StateMachine<Rig> machine;
    machine.Build<AlphaState, BetaState>(rig);
    machine.Change<BetaState>(rig);
    rig.log.clear();

    machine.Reset();

    EXPECT_TRUE(machine.IsCurrent<AlphaState>());
    EXPECT_TRUE(rig.log.empty());
}

// 組んだ並びが持てる状態の全集合。並びに入れていない型へは移れない
TEST(StateMachineTest, ChangeOnlyReachesStatesInTheBuiltList)
{
    Rig rig;
    NS::Obj::StateMachine<Rig> machine;
    machine.Build<AlphaState, BetaState>(rig);

    EXPECT_FALSE(machine.Change<GammaState>(rig));
    EXPECT_TRUE(machine.IsCurrent<AlphaState>());
}

TEST(StateMachineTest, NotBuiltMachineDoesNothing)
{
    Rig rig;
    NS::Obj::StateMachine<Rig> machine;

    EXPECT_FALSE(machine.IsBuilt());
    EXPECT_FALSE(machine.IsCurrent<AlphaState>());
    EXPECT_STREQ(machine.CurrentName(), "");

    machine.Step(rig, 0.016f);
    EXPECT_FALSE(machine.Change<AlphaState>(rig));
    EXPECT_TRUE(rig.log.empty());
}
