#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/StateMachineComponent.h"
#include "Runtime/Object/IUse/IUseState.h"
#include "Runtime/Object/StateMachine.h"

#include <gtest/gtest.h>

namespace
{
    class ActorState final : public NS::Obj::StateOf<ActorState, NS::Obj::Actor>
    {
    public:
        void OnStep(NS::Obj::Actor&, float) override {}
    };
} // namespace

TEST(StateMachineComponent, ActorStateWindowUsesOwnedComponent)
{
    NS::Obj::Actor actor;
    EXPECT_EQ(actor.GetStateMachine(), nullptr);
    NS::Obj::StateMachineComponent* component =
        NS::Obj::ComponentCast<NS::Obj::StateMachineComponent>(actor.CreatePart("StateMachine"));
    component->Build<ActorState>();

    ASSERT_NE(actor.GetStateMachine(), nullptr);
    EXPECT_TRUE(NS::Obj::IsState<ActorState>(actor));
    EXPECT_TRUE(NS::Obj::IsFirstStep(actor));
    component->OnUpdate();
    EXPECT_EQ(NS::Obj::StateStep(actor), 1u);
    EXPECT_TRUE(NS::Obj::SetState<ActorState>(actor));
}
