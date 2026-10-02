#include "Runtime/Object/Actor.h"
#include "Runtime/Object/IUse/IUseState.h"
#include "Runtime/Object/StateMachine.h"

#include <gtest/gtest.h>

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
        NS::Obj::StateMachine<NS::Obj::Actor>& BuildOwned()
        {
            return BuildStateMachine<NS::Obj::Actor, OwnedState>(*this);
        }
    };
} // namespace

TEST(ActorStateMachine, ActorWithoutMachineUpdatesQuietly)
{
    NS::Obj::Actor actor;
    EXPECT_EQ(actor.GetStateMachine(), nullptr);
    actor.Update();
    EXPECT_EQ(actor.GetStateMachine(), nullptr);
    EXPECT_FALSE(NS::Obj::IsState<OwnedState>(actor));
}

TEST(ActorStateMachine, BaseOwnsTheMachineAndUpdateStepsIt)
{
    MachineActor actor;
    NS::Obj::StateMachine<NS::Obj::Actor>& machine = actor.BuildOwned();

    ASSERT_NE(actor.GetStateMachine(), nullptr);
    EXPECT_EQ(actor.GetStateMachine(), &machine);
    EXPECT_TRUE(NS::Obj::IsState<OwnedState>(actor));
    EXPECT_TRUE(NS::Obj::IsFirstStep(actor));
    actor.Update();
    EXPECT_EQ(NS::Obj::StateStep(actor), 1u);
    actor.Update();
    EXPECT_EQ(NS::Obj::StateStep(actor), 2u);
    EXPECT_TRUE(NS::Obj::SetState<OwnedState>(actor));
}
