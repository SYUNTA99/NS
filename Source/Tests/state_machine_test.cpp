#include "Runtime/Core/Coroutine.h"
#include "Runtime/Object/StateMachine.h"

#include <gtest/gtest.h>

#include <vector>

namespace
{
    struct StateOwner;
    class IdleState;
    class ActiveState;
    class DeltaState;

    struct StateOwner
    {
        NS::Obj::StateMachine<StateOwner>* machine = nullptr;
        NS::Obj::SubStateMachine<StateOwner>* child = nullptr;
        std::vector<int> calls;
        bool change = false;
        bool coroutineChangesState = false;
        bool entryChangesState = false;
        bool exitChangesState = false;
        bool recordAfterChange = false;
        bool finishAfterChange = false;
        bool finishFromCoroutine = false;
        bool finishOnEntry = false;
        int resumed = 0;
        std::vector<float> deltas;
        const StateOwner* activeEnteredBy = nullptr;
        const StateOwner* activeSteppedBy = nullptr;
    };

    NS::Core::Coroutine ResumeLater(StateOwner& owner)
    {
        co_await NS::Core::NextFrame{};
        ++owner.resumed;
        if (owner.coroutineChangesState)
        {
            owner.machine->Change<ActiveState>();
        }
        if (owner.finishFromCoroutine)
        {
            owner.child->Finish();
        }
    }

    NS::Core::Coroutine ResumeChildLater(StateOwner& owner)
    {
        co_await NS::Core::WaitSeconds{1.0f};
        ++owner.resumed;
    }

    class IdleState final : public NS::Obj::StateOf<IdleState, StateOwner>
    {
    public:
        void OnEnter(StateOwner& owner) override
        {
            owner.calls.push_back(1);
            StartCoroutine(ResumeLater(owner));
        }

        void OnStep(StateOwner& owner, float) override
        {
            owner.calls.push_back(2);
            if (owner.change)
            {
                owner.machine->Change<ActiveState>();
                if (owner.recordAfterChange)
                {
                    owner.calls.push_back(7);
                }
                if (owner.finishAfterChange)
                {
                    owner.child->Finish();
                }
            }
        }

        void OnExit(StateOwner& owner) override
        {
            owner.calls.push_back(3);
            if (owner.exitChangesState)
            {
                owner.exitChangesState = false;
                owner.machine->Change<DeltaState>();
            }
        }
    };

    class ActiveState final : public NS::Obj::StateOf<ActiveState, StateOwner>
    {
    public:
        void OnEnter(StateOwner& owner) override
        {
            owner.calls.push_back(4);
            owner.activeEnteredBy = &owner;
            if (owner.finishOnEntry)
            {
                owner.child->Finish();
            }
            if (owner.entryChangesState)
            {
                owner.machine->Change<DeltaState>();
            }
        }
        void OnStep(StateOwner& owner, float) override
        {
            owner.calls.push_back(5);
            owner.activeSteppedBy = &owner;
        }
    };

    class ChildState final : public NS::Obj::StateOf<ChildState, StateOwner>
    {
    public:
        void OnEnter(StateOwner& owner) override { StartCoroutine(ResumeChildLater(owner)); }
        void OnStep(StateOwner& owner, float) override
        {
            owner.calls.push_back(6);
            owner.child->Finish();
        }
    };

    class DeltaState final : public NS::Obj::StateOf<DeltaState, StateOwner>
    {
    public:
        void OnEnter(StateOwner& owner) override { StartCoroutine(Run(owner)); }
        void OnStep(StateOwner&, float) override {}

    private:
        NS::Core::Coroutine Run(StateOwner& owner)
        {
            for (int step = 0; step < 2; ++step)
            {
                co_await NS::Core::NextFrame{};
                owner.deltas.push_back(StepDelta());
            }
        }
    };

} // namespace

TEST(StateMachine, TransitionOrderAndFirstStep)
{
    StateOwner owner;
    NS::Obj::StateMachine<StateOwner> machine;
    owner.machine = &machine;
    machine.Build<IdleState, ActiveState>(owner);
    EXPECT_TRUE(machine.IsFirstStep());
    EXPECT_EQ(machine.StepsInState(), 0u);

    machine.Step(1.0f / 60.0f);
    EXPECT_EQ(machine.StepsInState(), 1u);
    EXPECT_FALSE(machine.IsFirstStep());

    owner.change = true;
    machine.Step(1.0f / 60.0f);
    EXPECT_TRUE(machine.IsCurrent<ActiveState>());
    EXPECT_EQ(machine.StepsInState(), 0u);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 2, 3, 4}));

    machine.Step(1.0f / 60.0f);
    EXPECT_EQ(machine.StepsInState(), 1u);
    EXPECT_EQ(owner.calls.back(), 5);
}

TEST(StateMachine, OwnerComesFromBuildOnly)
{
    StateOwner owner;
    NS::Obj::StateMachine<StateOwner> machine;
    owner.machine = &machine;
    machine.Build<IdleState, ActiveState>(owner);
    ASSERT_TRUE(machine.Change<ActiveState>());
    machine.Step(1.0f / 60.0f);
    EXPECT_EQ(owner.activeEnteredBy, &owner);
    EXPECT_EQ(owner.activeSteppedBy, &owner);
}

TEST(StateMachine, LeavingStateCancelsItsCoroutine)
{
    StateOwner owner;
    NS::Obj::StateMachine<StateOwner> machine;
    owner.machine = &machine;
    machine.Build<IdleState, ActiveState>(owner);
    ASSERT_TRUE(machine.Change<ActiveState>());
    machine.Step(1.0f / 60.0f);
    EXPECT_EQ(owner.resumed, 0);
}

TEST(StateMachine, CoroutineCanLeaveItsStateWhileResuming)
{
    StateOwner owner;
    NS::Obj::StateMachine<StateOwner> machine;
    owner.machine = &machine;
    owner.coroutineChangesState = true;
    machine.Build<IdleState, ActiveState>(owner);
    machine.Step(1.0f / 60.0f);
    EXPECT_TRUE(machine.IsCurrent<ActiveState>());
    EXPECT_EQ(owner.resumed, 1);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 3, 4}));
}

TEST(StateMachine, ChildFinishStopsStepsAndCancelsCoroutine)
{
    StateOwner owner;
    NS::Obj::SubStateMachine<StateOwner> child;
    owner.child = &child;
    child.Build<ChildState>(owner);
    EXPECT_FALSE(child.IsDead());
    child.Step(1.0f / 60.0f);
    EXPECT_TRUE(child.IsDead());
    EXPECT_EQ(owner.calls, (std::vector<int>{6}));
    child.Step(1.0f / 60.0f);
    EXPECT_EQ(owner.calls, (std::vector<int>{6}));
    child.Machine().Step(2.0f);
    EXPECT_EQ(owner.resumed, 0);
}

TEST(StateMachine, CoroutineReadsTheCurrentStepDelta)
{
    StateOwner owner;
    NS::Obj::StateMachine<StateOwner> machine;
    machine.Build<DeltaState>(owner);
    EXPECT_TRUE(owner.deltas.empty());
    machine.Step(0.1f);
    machine.Step(0.2f);
    EXPECT_EQ(owner.deltas, (std::vector<float>{0.1f, 0.2f}));
}

TEST(StateMachine, ExternalRequestDefersEntryUntilTheNextStepBoundary)
{
    StateOwner owner;
    NS::Obj::StateMachine<StateOwner> machine;
    owner.machine = &machine;
    machine.Build<IdleState, ActiveState>(owner);
    ASSERT_TRUE(machine.Change<ActiveState>());
    EXPECT_TRUE(machine.IsCurrent<ActiveState>());
    EXPECT_FALSE(machine.IsFirstStep());
    EXPECT_EQ(machine.StepsInState(), static_cast<std::uint32_t>(-1));
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 3}));
    machine.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 3, 4, 5}));
    EXPECT_EQ(machine.StepsInState(), 1u);
    EXPECT_EQ(owner.resumed, 0);
}

TEST(StateMachine, EntryDoesNotInterruptThePreviousStep)
{
    StateOwner owner;
    NS::Obj::StateMachine<StateOwner> machine;
    owner.machine = &machine;
    owner.change = true;
    owner.recordAfterChange = true;
    machine.Build<IdleState, ActiveState>(owner);
    machine.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3, 7, 4}));
    EXPECT_TRUE(machine.IsFirstStep());
    EXPECT_EQ(machine.StepsInState(), 0u);
}

TEST(StateMachine, LastValidRequestWinsWithoutExitingTwice)
{
    StateOwner owner;
    NS::Obj::StateMachine<StateOwner> machine;
    owner.machine = &machine;
    machine.Build<IdleState, ActiveState, DeltaState>(owner);
    ASSERT_TRUE(machine.Change<ActiveState>());
    ASSERT_TRUE(machine.Change<DeltaState>());
    ASSERT_TRUE(machine.Change<DeltaState>());
    EXPECT_FALSE(machine.Change<ChildState>());
    EXPECT_TRUE(machine.IsCurrent<DeltaState>());
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 3}));
    machine.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 3}));
    EXPECT_EQ(owner.deltas, (std::vector<float>{0.1f}));
}

TEST(StateMachine, ResetDiscardsAPendingEntry)
{
    StateOwner owner;
    NS::Obj::StateMachine<StateOwner> machine;
    owner.machine = &machine;
    machine.Build<IdleState, ActiveState>(owner);
    machine.Change<ActiveState>();
    machine.Reset();
    machine.Step(0.1f);
    EXPECT_TRUE(machine.IsCurrent<IdleState>());
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 3, 2}));
    EXPECT_EQ(owner.resumed, 0);
}

TEST(StateMachine, ChildFinishDiscardsAnEntryRequestedDuringItsStep)
{
    StateOwner owner;
    NS::Obj::SubStateMachine<StateOwner> child;
    owner.child = &child;
    owner.machine = &child.Machine();
    owner.change = true;
    owner.finishAfterChange = true;
    child.Build<IdleState, ActiveState>(owner);
    child.Step(0.1f);
    EXPECT_TRUE(child.IsDead());
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3}));
    owner.change = false;
    child.Machine().Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3, 2}));
}

TEST(StateMachine, ReturningToTheExitedStateReentersAtTheBoundary)
{
    StateOwner owner;
    NS::Obj::StateMachine<StateOwner> machine;
    owner.machine = &machine;
    machine.Build<IdleState, ActiveState>(owner);
    machine.Change<ActiveState>();
    machine.Change<IdleState>();
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 3}));
    machine.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 3, 1, 2}));
}

TEST(StateMachine, RequestingTheCurrentStateDoesNotResetItsStep)
{
    StateOwner owner;
    NS::Obj::StateMachine<StateOwner> machine;
    owner.machine = &machine;
    machine.Build<IdleState, ActiveState>(owner);
    machine.Step(0.1f);
    machine.Change<IdleState>();
    EXPECT_EQ(machine.StepsInState(), 1u);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2}));
}

TEST(StateMachine, EntryRequestAtTheOpeningBoundaryDoesNotStepTheExitedState)
{
    StateOwner owner;
    NS::Obj::StateMachine<StateOwner> machine;
    owner.machine = &machine;
    owner.entryChangesState = true;
    machine.Build<IdleState, ActiveState, DeltaState>(owner);
    machine.Change<ActiveState>();
    machine.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 3, 4}));
    EXPECT_TRUE(machine.IsCurrent<DeltaState>());
    EXPECT_TRUE(machine.IsFirstStep());
    EXPECT_TRUE(owner.deltas.empty());
    machine.Step(0.2f);
    EXPECT_EQ(owner.deltas, (std::vector<float>{0.2f}));
}

TEST(StateMachine, EntryRequestAtTheClosingBoundaryWaitsUntilTheNextStep)
{
    StateOwner owner;
    NS::Obj::StateMachine<StateOwner> machine;
    owner.machine = &machine;
    owner.entryChangesState = true;
    owner.change = true;
    machine.Build<IdleState, ActiveState, DeltaState>(owner);
    machine.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3, 4}));
    EXPECT_TRUE(machine.IsCurrent<DeltaState>());
    EXPECT_FALSE(machine.IsFirstStep());
    EXPECT_EQ(machine.StepsInState(), static_cast<std::uint32_t>(-1));
    EXPECT_TRUE(owner.deltas.empty());
    machine.Step(0.2f);
    EXPECT_EQ(owner.deltas, (std::vector<float>{0.2f}));
}

TEST(StateMachine, ExitCanReplaceThePendingTargetWithoutRecursiveExit)
{
    StateOwner owner;
    NS::Obj::StateMachine<StateOwner> machine;
    owner.machine = &machine;
    owner.exitChangesState = true;
    machine.Build<IdleState, ActiveState, DeltaState>(owner);
    machine.Change<ActiveState>();
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 3}));
    EXPECT_TRUE(machine.IsCurrent<DeltaState>());
    machine.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 3}));
    EXPECT_EQ(owner.deltas, (std::vector<float>{0.1f}));
}

TEST(StateMachine, RebuildDiscardsThePreviousPendingEntry)
{
    StateOwner owner;
    NS::Obj::StateMachine<StateOwner> machine;
    owner.machine = &machine;
    machine.Build<IdleState, ActiveState>(owner);
    machine.Change<ActiveState>();
    machine.Build<IdleState, ActiveState>(owner);
    machine.Step(0.1f);
    EXPECT_TRUE(machine.IsCurrent<IdleState>());
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 3, 1, 2}));
    EXPECT_EQ(owner.resumed, 1);
}

TEST(StateMachine, ChildFinishDuringCoroutineStopsTheRemainingStateStep)
{
    StateOwner owner;
    NS::Obj::SubStateMachine<StateOwner> child;
    owner.child = &child;
    owner.machine = &child.Machine();
    owner.coroutineChangesState = true;
    owner.finishFromCoroutine = true;
    child.Build<IdleState, ActiveState>(owner);
    child.Step(0.1f);
    EXPECT_TRUE(child.IsDead());
    EXPECT_EQ(owner.resumed, 1);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 3}));
    EXPECT_EQ(child.Machine().StepsInState(), 0u);
    child.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 3}));
}

TEST(StateMachine, ChildFinishDuringEntryStopsTheFirstStateStep)
{
    StateOwner owner;
    NS::Obj::SubStateMachine<StateOwner> child;
    owner.child = &child;
    owner.machine = &child.Machine();
    owner.finishOnEntry = true;
    child.Build<IdleState, ActiveState>(owner);
    child.Machine().Change<ActiveState>();
    child.Step(0.1f);
    EXPECT_TRUE(child.IsDead());
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 3, 4}));
    EXPECT_EQ(child.Machine().StepsInState(), 0u);
    child.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 3, 4}));
}
