#include "Game/Level/PropActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/Object/IUse/IUseState.h"
#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Reflection/Archetype.h"
#include "NSlib/Object/Reflection/ObjectBuilder.h"
#include "NSlib/Object/Reflection/ReflectedSubObjs.h"
#include "NSlib/Object/SceneActor.h"
#include "NSlib/Object/StateMachineOf.h"
#include "NSlib/Object/SubObjects/StateMachine.h"

#include <gtest/gtest.h>

#include <memory>
#include <string_view>

#include "NSlib/Core/Types.h"

namespace
{
    int CountSubObjKey(const NS::Obj::SceneActor& actor, std::string_view key)
    {
        int count = 0;
        NS::Obj::ForEachReflectedSubObj(actor, [&](const NS::Obj::SubObjectDesc& field, const NS::Obj::SubObject*) {
            if (std::string_view(field.key) == key)
            {
                ++count;
            }
        });
        return count;
    }

    void ExpectNoStateMachineFields(const nlohmann::json& object)
    {
        const nlohmann::json* fields = NS::Obj::SubObjFields(object, "StateMachine");
        if (fields != nullptr)
        {
            EXPECT_TRUE(fields->empty()) << fields->dump();
        }
    }

    void ExpectOnlyEnabledField(const nlohmann::json& object)
    {
        const nlohmann::json* fields = NS::Obj::SubObjFields(object, "StateMachine");
        if (fields == nullptr)
        {
            return;
        }
        for (nlohmann::json::const_iterator field = fields->begin(); field != fields->end(); ++field)
        {
            EXPECT_EQ(field.key(), "enabled");
        }
    }

    class OwnedState final : public NS::Obj::StateOf<OwnedState, NS::Obj::SceneActor>
    {
    public:
        void OnStep(NS::Obj::SceneActor&, float) override {}
    };

    // BuildStateMachine は派生が自分の組み立ての中で呼ぶ口なので、試しの派生が開ける
    class MachineActor final : public NS::Obj::SceneActor
    {
    public:
        NS::Obj::StateMachineOf<NS::Obj::SceneActor>* BuildOwned()
        {
            NS::Obj::StateMachineOf<NS::Obj::SceneActor>* machine = nullptr;
            (void)BuildStateMachine<NS::Obj::SceneActor, OwnedState>(*this, machine);
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

    class ProbeActor final : public NS::Obj::SceneActor
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
    class StepReadingActor final : public NS::Obj::SceneActor
    {
    public:
        void BuildOwned()
        {
            NS::Obj::StateMachineOf<NS::Obj::SceneActor>* machine = nullptr;
            (void)BuildStateMachine<NS::Obj::SceneActor, OwnedState>(*this, machine);
        }
        NS::uint32 stepsSeenInVisual = 0;

    protected:
        void VisualStep() override
        {
            NS::Obj::SceneActor::VisualStep();
            stepsSeenInVisual = StepsInState(this);
        }
    };
} // namespace

TEST(ActorStateMachine, ActorWithoutMachineUpdatesQuietly)
{
    NS::Obj::SceneActor actor;
    EXPECT_EQ(actor.GetStates(), nullptr);
    actor.Update();
    EXPECT_EQ(actor.GetStates(), nullptr);
    EXPECT_FALSE(NS::Obj::IsState<OwnedState>(&actor));
}

TEST(ActorStateMachine, BaseOwnsTheMachineAndUpdateStepsIt)
{
    MachineActor actor;
    NS::Obj::StateMachineOf<NS::Obj::SceneActor>* machine = actor.BuildOwned();

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

TEST(ActorStateMachine, MachineIsListedAsTheStateMachineSubObject)
{
    const std::unique_ptr<PlayerActor> player = NS::Obj::CreateActor<PlayerActor>();
    EXPECT_EQ(CountSubObjKey(*player, "StateMachine"), 1);
    const NS::Obj::StateMachine* playerPart = NS::Obj::Cast<NS::Obj::StateMachine>(player->FindSubObj("StateMachine"));
    ASSERT_NE(playerPart, nullptr);
    ASSERT_NE(player->GetStates(), nullptr);
    EXPECT_EQ(playerPart->Machine(), player->GetStates());

    const std::unique_ptr<GL::Level::PropActor> prop = NS::Obj::CreateActor<GL::Level::PropActor>();
    EXPECT_EQ(CountSubObjKey(*prop, "StateMachine"), 1);
    const NS::Obj::StateMachine* propPart = NS::Obj::Cast<NS::Obj::StateMachine>(prop->FindSubObj("StateMachine"));
    ASSERT_NE(propPart, nullptr);
    ASSERT_NE(prop->GetStates(), nullptr);
    EXPECT_EQ(propPart->Machine(), prop->GetStates());
}

TEST(ActorStateMachine, SavedJsonGainsNoStateMachineFields)
{
    const std::unique_ptr<PlayerActor> player = NS::Obj::CreateActor<PlayerActor>();
    ExpectNoStateMachineFields(NS::Obj::ObjectToJson(*player));
    ExpectOnlyEnabledField(NS::Obj::ExpandObjectJson(NS::Obj::MakePrototypeJson<PlayerActor>()));

    const std::unique_ptr<GL::Level::PropActor> prop = NS::Obj::CreateActor<GL::Level::PropActor>();
    ExpectNoStateMachineFields(NS::Obj::ObjectToJson(*prop));
    ExpectOnlyEnabledField(NS::Obj::ExpandObjectJson(NS::Obj::MakePrototypeJson<GL::Level::PropActor>()));
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
