#include "NSlib/Object/Actor.h"
#include "NSlib/Object/IUse/IUseState.h"
#include "NSlib/Object/StateMachineOf.h"

#include <gtest/gtest.h>

#include "NSlib/Core/Types.h"

namespace
{
    class OwnedState final : public NS::Obj::StateOf<OwnedState, NS::Obj::Actor>
    {
    public:
        void OnStep(NS::Obj::Actor&, float) override {}
    };

    // BuildStateMachine は派生が自分の組み立ての中で呼ぶ口なので、試しの派生が開ける
    class MachineActor final : public NS::Obj::Actor
    {
    public:
        NS::Obj::StateMachineOf<NS::Obj::Actor>* BuildOwned()
        {
            NS::Obj::StateMachineOf<NS::Obj::Actor>* machine = nullptr;
            (void)BuildStateMachine<NS::Obj::Actor, OwnedState>(*this, machine);
            return machine;
        }
    };

    class ProbeActor;

    // 先頭の OnEnter で、持ち主が預かった型付きの状態機械を引けるかを控える
    class ProbeState final : public NS::Obj::StateOf<ProbeState, ProbeActor>
    {
    public:
        void OnEnter(ProbeActor& owner) override;
        void OnStep(ProbeActor&, float) override {}
    };

    class SecondProbeState final : public NS::Obj::StateOf<SecondProbeState, ProbeActor>
    {
    public:
        void OnStep(ProbeActor&, float) override {}
    };

    class ProbeActor final : public NS::Obj::Actor
    {
    public:
        bool BuildProbe() { return BuildStateMachine<ProbeActor, ProbeState>(*this, machine); }
        bool BuildSecond() { return BuildStateMachine<ProbeActor, SecondProbeState>(*this, machine); }
        NS::Obj::StateMachineOf<ProbeActor>* machine = nullptr;
        NS::Obj::StateMachineOf<ProbeActor>* machineSeenInEnter = nullptr;
    };

    void ProbeState::OnEnter(ProbeActor& owner)
    {
        owner.machineSeenInEnter = owner.machine;
    }

    // 段のメンバーの中から、歩数の問いを修飾なしで呼ぶ
    class StepReadingActor final : public NS::Obj::Actor
    {
    public:
        void BuildOwned()
        {
            NS::Obj::StateMachineOf<NS::Obj::Actor>* machine = nullptr;
            (void)BuildStateMachine<NS::Obj::Actor, OwnedState>(*this, machine);
        }
        NS::uint32 stepsSeenInVisual = 0;

    protected:
        void VisualStep() override
        {
            NS::Obj::Actor::VisualStep();
            stepsSeenInVisual = StepsInState(this);
        }
    };
} // namespace

TEST(ActorStateMachine, ActorWithoutMachineUpdatesQuietly)
{
    NS::Obj::Actor actor;
    EXPECT_EQ(actor.GetStates(), nullptr);
    actor.Update();
    EXPECT_EQ(actor.GetStates(), nullptr);
    EXPECT_FALSE(NS::Obj::IsState<OwnedState>(&actor));
}

TEST(ActorStateMachine, BaseOwnsTheMachineAndUpdateStepsIt)
{
    MachineActor actor;
    NS::Obj::StateMachineOf<NS::Obj::Actor>* machine = actor.BuildOwned();

    ASSERT_NE(actor.GetStates(), nullptr);
    EXPECT_EQ(actor.GetStates(), machine);
    EXPECT_TRUE(NS::Obj::IsState<OwnedState>(&actor));
    EXPECT_TRUE(NS::Obj::IsFirstStep(&actor));
    actor.Update();
    EXPECT_EQ(NS::Obj::StepsInState(&actor), 1u);
    actor.Update();
    EXPECT_EQ(NS::Obj::StepsInState(&actor), 2u);
    EXPECT_TRUE(NS::Obj::SetState<OwnedState>(&actor));
}

TEST(ActorStateMachine, StepsInStateIsReachableUnqualifiedInsideAnActor)
{
    StepReadingActor actor;
    actor.BuildOwned();

    actor.Update();
    EXPECT_EQ(actor.stepsSeenInVisual, 1u);
    actor.Update();
    EXPECT_EQ(actor.stepsSeenInVisual, 2u);
}

TEST(ActorStateMachine, TypedPointerIsReadableFromFirstStateOnEnter)
{
    ProbeActor actor;
    ASSERT_TRUE(actor.BuildProbe());

    ASSERT_NE(actor.machine, nullptr);
    EXPECT_EQ(actor.machineSeenInEnter, actor.machine);
    EXPECT_EQ(actor.GetStates(), actor.machine);
}

TEST(ActorStateMachine, SecondBuildFailsAndKeepsTheFirstMachine)
{
    ProbeActor actor;
    ASSERT_TRUE(actor.BuildProbe());
    NS::Obj::StateMachineOf<ProbeActor>* first = actor.machine;

    EXPECT_FALSE(actor.BuildSecond());

    EXPECT_EQ(actor.machine, first);
    EXPECT_EQ(actor.GetStates(), first);
    EXPECT_TRUE(NS::Obj::IsState<ProbeState>(&actor));
}
