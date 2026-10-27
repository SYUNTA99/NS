#include "NSlib/Core/Coroutine.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/StateMachineOf.h"

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
        NS::StateMachineOf<StateOwner>* machine = nullptr;
        NS::SubStateMachine<StateOwner>* child = nullptr;
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

    NS::Coroutine ResumeLater(StateOwner& owner)
    {
        co_await NS::NextFrame{};
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

    NS::Coroutine ResumeChildLater(StateOwner& owner)
    {
        co_await NS::WaitSeconds{1.0f};
        ++owner.resumed;
    }

    class IdleState final : public NS::StateOf<IdleState, StateOwner>
    {
    public:
        void OnEnter(StateOwner* owner) override
        {
            NS_ASSERT(Scene, owner != nullptr, "状態の持ち主が無い");
            owner->calls.push_back(1);
            StartCoroutine(ResumeLater(*owner));
        }

        void OnStep(StateOwner* owner, float) override
        {
            NS_ASSERT(Scene, owner != nullptr, "状態の持ち主が無い");
            owner->calls.push_back(2);
            if (owner->change)
            {
                owner->machine->Change<ActiveState>();
                if (owner->recordAfterChange)
                {
                    owner->calls.push_back(7);
                }
                if (owner->finishAfterChange)
                {
                    owner->child->Finish();
                }
            }
        }

        void OnExit(StateOwner* owner) override
        {
            NS_ASSERT(Scene, owner != nullptr, "状態の持ち主が無い");
            owner->calls.push_back(3);
            if (owner->exitChangesState)
            {
                owner->exitChangesState = false;
                owner->machine->Change<DeltaState>();
            }
        }
    };

    class ActiveState final : public NS::StateOf<ActiveState, StateOwner>
    {
    public:
        void OnEnter(StateOwner* owner) override
        {
            NS_ASSERT(Scene, owner != nullptr, "状態の持ち主が無い");
            owner->calls.push_back(4);
            owner->activeEnteredBy = owner;
            if (owner->finishOnEntry)
            {
                owner->child->Finish();
            }
            if (owner->entryChangesState)
            {
                owner->machine->Change<DeltaState>();
            }
        }
        void OnStep(StateOwner* owner, float) override
        {
            NS_ASSERT(Scene, owner != nullptr, "状態の持ち主が無い");
            owner->calls.push_back(5);
            owner->activeSteppedBy = owner;
        }
    };

    class ChildState final : public NS::StateOf<ChildState, StateOwner>
    {
    public:
        void OnEnter(StateOwner* owner) override
        {
            NS_ASSERT(Scene, owner != nullptr, "状態の持ち主が無い");
            StartCoroutine(ResumeChildLater(*owner));
        }
        void OnStep(StateOwner* owner, float) override
        {
            NS_ASSERT(Scene, owner != nullptr, "状態の持ち主が無い");
            owner->calls.push_back(6);
            owner->child->Finish();
        }
    };

    class DeltaState final : public NS::StateOf<DeltaState, StateOwner>
    {
    public:
        void OnEnter(StateOwner* owner) override
        {
            NS_ASSERT(Scene, owner != nullptr, "状態の持ち主が無い");
            StartCoroutine(Run(*owner));
        }
        void OnStep(StateOwner* owner, float) override
        {
            NS_ASSERT(Scene, owner != nullptr, "状態の持ち主が無い");
            (void)owner;
        }

    private:
        NS::Coroutine Run(StateOwner& owner)
        {
            for (int step = 0; step < 2; ++step)
            {
                co_await NS::NextFrame{};
                owner.deltas.push_back(StepDelta());
            }
        }
    };

} // namespace

TEST(StateMachineOf, TransitionOrderAndFirstStep)
{
    StateOwner owner;
    NS::StateMachineOf<StateOwner> machine;
    owner.machine = &machine;
    machine.Build<IdleState, ActiveState>(&owner);

    machine.Step(1.0f / 60.0f);
    EXPECT_EQ(machine.StepCount(), 1u);
    EXPECT_FALSE(machine.IsFirstStep());

    owner.change = true;
    machine.Step(1.0f / 60.0f);
    EXPECT_TRUE(machine.IsCurrent<ActiveState>());
    EXPECT_EQ(machine.StepCount(), 0u);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 2, 3, 4}));

    machine.Step(1.0f / 60.0f);
    EXPECT_EQ(machine.StepCount(), 1u);
    EXPECT_EQ(owner.calls.back(), 5);
}

TEST(StateMachineOf, OwnerComesFromBuildOnly)
{
    StateOwner owner;
    NS::StateMachineOf<StateOwner> machine;
    owner.machine = &machine;
    machine.Build<IdleState, ActiveState>(&owner);
    ASSERT_TRUE(machine.Change<ActiveState>());
    machine.Step(1.0f / 60.0f);
    EXPECT_EQ(owner.activeEnteredBy, &owner);
    EXPECT_EQ(owner.activeSteppedBy, &owner);
}

TEST(StateMachineOf, BuildEntersTheFirstStateAtTheFirstStep)
{
    StateOwner owner;
    NS::StateMachineOf<StateOwner> machine;
    owner.machine = &machine;
    machine.Build<IdleState, ActiveState>(&owner);
    EXPECT_TRUE(machine.IsBuilt());
    EXPECT_TRUE(machine.IsCurrent<IdleState>());
    EXPECT_FALSE(machine.IsFirstStep());
    EXPECT_TRUE(owner.calls.empty());

    machine.Step(1.0f / 60.0f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2}));
    EXPECT_EQ(machine.StepCount(), 1u);
}

TEST(StateMachineOf, ResetEntersTheFirstStateAgainWithoutExiting)
{
    StateOwner owner;
    NS::StateMachineOf<StateOwner> machine;
    owner.machine = &machine;
    owner.change = true;
    machine.Build<IdleState, ActiveState>(&owner);
    machine.Step(0.1f);
    owner.change = false;
    machine.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3, 4, 5}));

    machine.Reset();
    EXPECT_TRUE(machine.IsCurrent<IdleState>());
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3, 4, 5}));
    machine.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3, 4, 5, 1, 2}));
    EXPECT_EQ(machine.StepCount(), 1u);
}

// Reset は今の状態のコルーチンを捨てる。入り直した先頭の状態のコルーチンだけが続く
TEST(StateMachineOf, ResetCancelsTheCurrentStateCoroutine)
{
    StateOwner owner;
    NS::StateMachineOf<StateOwner> machine;
    machine.Build<DeltaState, ActiveState>(&owner);
    machine.Step(0.1f);
    ASSERT_EQ(owner.deltas, (std::vector<float>{0.1f}));
    machine.Reset();
    machine.Step(0.2f);
    EXPECT_EQ(owner.deltas, (std::vector<float>{0.1f, 0.2f}));
}

// 組み直すと、前の状態のコルーチンは続かない
TEST(StateMachineOf, RebuildCancelsThePreviousStateCoroutine)
{
    StateOwner owner;
    NS::StateMachineOf<StateOwner> machine;
    machine.Build<DeltaState, ActiveState>(&owner);
    machine.Step(0.1f);
    ASSERT_EQ(owner.deltas, (std::vector<float>{0.1f}));
    machine.Build<DeltaState, ActiveState>(&owner);
    machine.Step(0.2f);
    EXPECT_EQ(owner.deltas, (std::vector<float>{0.1f, 0.2f}));
}

// Build と Reset の直後にコルーチンを捨てても、先頭へ入る予約は残る
TEST(StateMachineOf, CancellingBeforeTheFirstStepKeepsTheFirstEntry)
{
    StateOwner owner;
    NS::StateMachineOf<StateOwner> machine;
    owner.machine = &machine;
    machine.Build<IdleState, ActiveState>(&owner);
    machine.CancelCurrentCoroutines();
    EXPECT_TRUE(machine.IsCurrent<IdleState>());
    machine.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2}));

    machine.Reset();
    machine.CancelCurrentCoroutines();
    EXPECT_TRUE(machine.IsCurrent<IdleState>());
    machine.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 1, 2}));
}

TEST(StateMachineOf, LeavingStateCancelsItsCoroutine)
{
    StateOwner owner;
    NS::StateMachineOf<StateOwner> machine;
    machine.Build<DeltaState, ActiveState>(&owner);
    machine.Step(0.1f);
    ASSERT_TRUE(machine.Change<ActiveState>());
    machine.Step(0.2f);
    EXPECT_EQ(owner.deltas, (std::vector<float>{0.1f}));
}

TEST(StateMachineOf, CoroutineCanLeaveItsStateWhileResuming)
{
    StateOwner owner;
    NS::StateMachineOf<StateOwner> machine;
    owner.machine = &machine;
    owner.coroutineChangesState = true;
    machine.Build<IdleState, ActiveState>(&owner);
    machine.Step(1.0f / 60.0f);
    EXPECT_TRUE(machine.IsCurrent<ActiveState>());
    EXPECT_EQ(owner.resumed, 1);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 3, 4}));
}

TEST(StateMachineOf, ChildFinishStopsStepsAndCancelsCoroutine)
{
    StateOwner owner;
    NS::SubStateMachine<StateOwner> child;
    owner.child = &child;
    child.Build<ChildState>(&owner);
    EXPECT_FALSE(child.IsDead());
    child.Step(1.0f / 60.0f);
    EXPECT_TRUE(child.IsDead());
    EXPECT_EQ(owner.calls, (std::vector<int>{6}));
    child.Step(1.0f / 60.0f);
    EXPECT_EQ(owner.calls, (std::vector<int>{6}));
    child.Machine().Step(2.0f);
    EXPECT_EQ(owner.resumed, 0);
}

TEST(StateMachineOf, CoroutineReadsTheCurrentStepDelta)
{
    StateOwner owner;
    NS::StateMachineOf<StateOwner> machine;
    machine.Build<DeltaState>(&owner);
    EXPECT_TRUE(owner.deltas.empty());
    machine.Step(0.1f);
    machine.Step(0.2f);
    EXPECT_EQ(owner.deltas, (std::vector<float>{0.1f, 0.2f}));
}

TEST(StateMachineOf, ExternalRequestDefersEntryUntilTheNextStepBoundary)
{
    StateOwner owner;
    NS::StateMachineOf<StateOwner> machine;
    owner.machine = &machine;
    machine.Build<IdleState, ActiveState>(&owner);
    machine.Step(0.1f);
    ASSERT_TRUE(machine.Change<ActiveState>());
    EXPECT_TRUE(machine.IsCurrent<ActiveState>());
    EXPECT_FALSE(machine.IsFirstStep());
    EXPECT_EQ(machine.StepCount(), static_cast<NS::uint32>(-1));
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3}));
    machine.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3, 4, 5}));
    EXPECT_EQ(machine.StepCount(), 1u);
}

TEST(StateMachineOf, EntryDoesNotInterruptThePreviousStep)
{
    StateOwner owner;
    NS::StateMachineOf<StateOwner> machine;
    owner.machine = &machine;
    owner.change = true;
    owner.recordAfterChange = true;
    machine.Build<IdleState, ActiveState>(&owner);
    machine.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3, 7, 4}));
    EXPECT_TRUE(machine.IsFirstStep());
    EXPECT_EQ(machine.StepCount(), 0u);
}

TEST(StateMachineOf, LastValidRequestWinsWithoutExitingTwice)
{
    StateOwner owner;
    NS::StateMachineOf<StateOwner> machine;
    owner.machine = &machine;
    machine.Build<IdleState, ActiveState, DeltaState>(&owner);
    machine.Step(0.1f);
    ASSERT_TRUE(machine.Change<ActiveState>());
    ASSERT_TRUE(machine.Change<DeltaState>());
    ASSERT_TRUE(machine.Change<DeltaState>());
    EXPECT_TRUE(machine.IsCurrent<DeltaState>());
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3}));
    machine.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3}));
    EXPECT_EQ(owner.deltas, (std::vector<float>{0.1f}));
}

TEST(StateMachineOf, ResetDiscardsAPendingEntry)
{
    StateOwner owner;
    NS::StateMachineOf<StateOwner> machine;
    owner.machine = &machine;
    machine.Build<IdleState, ActiveState>(&owner);
    machine.Step(0.1f);
    machine.Change<ActiveState>();
    machine.Reset();
    machine.Step(0.1f);
    EXPECT_TRUE(machine.IsCurrent<IdleState>());
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3, 1, 2}));
}

TEST(StateMachineOf, ChildFinishDiscardsAnEntryRequestedDuringItsStep)
{
    StateOwner owner;
    NS::SubStateMachine<StateOwner> child;
    owner.child = &child;
    owner.machine = &child.Machine();
    owner.change = true;
    owner.finishAfterChange = true;
    child.Build<IdleState, ActiveState>(&owner);
    child.Step(0.1f);
    EXPECT_TRUE(child.IsDead());
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3}));
    owner.change = false;
    child.Machine().Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3, 2}));
}

TEST(StateMachineOf, ReturningToTheExitedStateReentersAtTheBoundary)
{
    StateOwner owner;
    NS::StateMachineOf<StateOwner> machine;
    owner.machine = &machine;
    machine.Build<IdleState, ActiveState>(&owner);
    machine.Step(0.1f);
    machine.Change<ActiveState>();
    machine.Change<IdleState>();
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3}));
    machine.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3, 1, 2}));
}

TEST(StateMachineOf, RequestingTheCurrentStateDoesNotResetItsStep)
{
    StateOwner owner;
    NS::StateMachineOf<StateOwner> machine;
    owner.machine = &machine;
    machine.Build<IdleState, ActiveState>(&owner);
    machine.Step(0.1f);
    machine.Change<IdleState>();
    EXPECT_EQ(machine.StepCount(), 1u);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2}));
}

TEST(StateMachineOf, EntryRequestAtTheOpeningBoundaryDoesNotStepTheExitedState)
{
    StateOwner owner;
    NS::StateMachineOf<StateOwner> machine;
    owner.machine = &machine;
    owner.entryChangesState = true;
    machine.Build<IdleState, ActiveState, DeltaState>(&owner);
    machine.Step(0.1f);
    machine.Change<ActiveState>();
    machine.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3, 4}));
    EXPECT_TRUE(machine.IsCurrent<DeltaState>());
    EXPECT_TRUE(machine.IsFirstStep());
    EXPECT_TRUE(owner.deltas.empty());
    machine.Step(0.2f);
    EXPECT_EQ(owner.deltas, (std::vector<float>{0.2f}));
}

TEST(StateMachineOf, EntryRequestAtTheClosingBoundaryWaitsUntilTheNextStep)
{
    StateOwner owner;
    NS::StateMachineOf<StateOwner> machine;
    owner.machine = &machine;
    owner.entryChangesState = true;
    owner.change = true;
    machine.Build<IdleState, ActiveState, DeltaState>(&owner);
    machine.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3, 4}));
    EXPECT_TRUE(machine.IsCurrent<DeltaState>());
    EXPECT_FALSE(machine.IsFirstStep());
    EXPECT_EQ(machine.StepCount(), static_cast<NS::uint32>(-1));
    EXPECT_TRUE(owner.deltas.empty());
    machine.Step(0.2f);
    EXPECT_EQ(owner.deltas, (std::vector<float>{0.2f}));
}

TEST(StateMachineOf, ExitCanReplaceThePendingTargetWithoutRecursiveExit)
{
    StateOwner owner;
    NS::StateMachineOf<StateOwner> machine;
    owner.machine = &machine;
    owner.exitChangesState = true;
    machine.Build<IdleState, ActiveState, DeltaState>(&owner);
    machine.Step(0.1f);
    machine.Change<ActiveState>();
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3}));
    EXPECT_TRUE(machine.IsCurrent<DeltaState>());
    machine.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3}));
    EXPECT_EQ(owner.deltas, (std::vector<float>{0.1f}));
}

TEST(StateMachineOf, RebuildDiscardsThePreviousPendingEntry)
{
    StateOwner owner;
    NS::StateMachineOf<StateOwner> machine;
    owner.machine = &machine;
    machine.Build<IdleState, ActiveState>(&owner);
    machine.Step(0.1f);
    machine.Change<ActiveState>();
    machine.Build<IdleState, ActiveState>(&owner);
    machine.Step(0.1f);
    EXPECT_TRUE(machine.IsCurrent<IdleState>());
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3, 1, 2}));
}

TEST(StateMachineOf, ChildFinishDuringCoroutineStopsTheRemainingStateStep)
{
    StateOwner owner;
    NS::SubStateMachine<StateOwner> child;
    owner.child = &child;
    owner.machine = &child.Machine();
    owner.coroutineChangesState = true;
    owner.finishFromCoroutine = true;
    child.Build<IdleState, ActiveState>(&owner);
    child.Step(0.1f);
    EXPECT_TRUE(child.IsDead());
    EXPECT_EQ(owner.resumed, 1);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 3}));
    EXPECT_EQ(child.Machine().StepCount(), 0u);
    child.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 3}));
}

TEST(StateMachineOf, ChildFinishDuringEntryStopsTheFirstStateStep)
{
    StateOwner owner;
    NS::SubStateMachine<StateOwner> child;
    owner.child = &child;
    owner.machine = &child.Machine();
    owner.finishOnEntry = true;
    child.Build<IdleState, ActiveState>(&owner);
    child.Step(0.1f);
    child.Machine().Change<ActiveState>();
    child.Step(0.1f);
    EXPECT_TRUE(child.IsDead());
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3, 4}));
    EXPECT_EQ(child.Machine().StepCount(), 0u);
    child.Step(0.1f);
    EXPECT_EQ(owner.calls, (std::vector<int>{1, 2, 3, 4}));
}
