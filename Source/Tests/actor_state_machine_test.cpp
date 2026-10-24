#include "Editor/EditorActors.h"
#include "Game/Level/PropActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/StateMachine.h"
#include "NSlib/CoreObject/IUse/IUseState.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ActorBuilder.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/StateMachineOf.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include <memory>
#include <string_view>

#include "NSlib/Core/Types.h"

namespace
{
    int CountClassObjKey(const NS::SceneActor* actor, std::string_view key)
    {
        NS_ASSERT(Game, actor != nullptr, "CountClassObjKey: 渡したアクターか部品が無い");
        int count = 0;
        NS::ForEachReflectedClassObj(actor, [&](const NS::ClassObjInfo& field, const NS::Object*) {
            if (std::string_view(field.key) == key)
            {
                ++count;
            }
        });
        return count;
    }

    // 並びに鍵 key で出た部品。出なければ nullptr
    const NS::Object* ListedClassObj(const NS::SceneActor* actor, std::string_view key)
    {
        NS_ASSERT(Game, actor != nullptr, "ListedClassObj: 渡したアクターか部品が無い");
        const NS::Object* listed = nullptr;
        NS::ForEachReflectedClassObj(actor, [&](const NS::ClassObjInfo& field, const NS::Object* part) {
            if (std::string_view(field.key) == key)
            {
                listed = part;
            }
        });
        return listed;
    }

    void ExpectNoStateMachineFields(const nlohmann::json& object)
    {
        const nlohmann::json* fields = NS::ClassObjFields(object, "StateMachine");
        if (fields != nullptr)
        {
            EXPECT_TRUE(fields->empty()) << fields->dump();
        }
    }

    class OwnedState final : public NS::StateOf<OwnedState, NS::SceneActor>
    {
    public:
        void OnStep(NS::SceneActor* owner, float) override
        {
            NS_ASSERT(Game, owner != nullptr, "状態の持ち主が無い");
            (void)owner;
        }
    };

    // BuildStateMachine は派生が自分の組み立ての中で呼ぶ口なので、試しの派生が開ける
    class MachineActor final : public NS::SceneActor
    {
    public:
        NS::StateMachineOf<NS::SceneActor>* BuildOwned()
        {
            NS::StateMachineOf<NS::SceneActor>* machine = nullptr;
            (void)BuildStateMachine<NS::SceneActor, OwnedState>(this, machine);
            return machine;
        }
    };

    class ProbeActor;

    // 先頭の OnEnter で、持ち主が預かった型付きの状態機械を引けるかを控える
    class ProbeState final : public NS::StateOf<ProbeState, ProbeActor>
    {
    public:
        void OnEnter(ProbeActor* owner) override;
        void OnStep(ProbeActor* owner, float) override
        {
            NS_ASSERT(Game, owner != nullptr, "状態の持ち主が無い");
            (void)owner;
        }
    };

    class SecondProbeState final : public NS::StateOf<SecondProbeState, ProbeActor>
    {
    public:
        void OnStep(ProbeActor* owner, float) override
        {
            NS_ASSERT(Game, owner != nullptr, "状態の持ち主が無い");
            (void)owner;
        }
    };

    class ProbeActor final : public NS::SceneActor
    {
    public:
        bool BuildProbe() { return BuildStateMachine<ProbeActor, ProbeState>(this, machine); }
        bool BuildSecond() { return BuildStateMachine<ProbeActor, SecondProbeState>(this, machine); }
        NS::StateMachineOf<ProbeActor>* machine = nullptr;
        NS::StateMachineOf<ProbeActor>* machineSeenInEnter = nullptr;
    };

    void ProbeState::OnEnter(ProbeActor* owner)
    {
        NS_ASSERT(Game, owner != nullptr, "状態の持ち主が無い");
        owner->machineSeenInEnter = owner->machine;
    }

    // 段のメンバーの中から、歩数の問いを修飾なしで呼ぶ
    class StepReadingActor final : public NS::SceneActor
    {
    public:
        void BuildOwned()
        {
            NS::StateMachineOf<NS::SceneActor>* machine = nullptr;
            (void)BuildStateMachine<NS::SceneActor, OwnedState>(this, machine);
        }
        NS::uint32 stepsSeenInVisual = 0;

    protected:
        void VisualStep() override
        {
            NS::SceneActor::VisualStep();
            stepsSeenInVisual = GetStateStep(this);
        }
    };
} // namespace

TEST(ActorStateMachine, ActorWithoutMachineUpdatesQuietly)
{
    NS::SceneActor actor;
    EXPECT_EQ(actor.GetStateMachine(), nullptr);
    actor.Update();
    EXPECT_EQ(actor.GetStateMachine(), nullptr);
    EXPECT_FALSE(NS::IsState<OwnedState>(&actor));
}

TEST(ActorStateMachine, BaseOwnsTheMachineAndUpdateStepsIt)
{
    MachineActor actor;
    NS::StateMachineOf<NS::SceneActor>* machine = actor.BuildOwned();

    ASSERT_NE(actor.GetStateMachine(), nullptr);
    EXPECT_EQ(actor.GetStateMachine(), machine);
    EXPECT_TRUE(NS::IsState<OwnedState>(&actor));
    EXPECT_TRUE(NS::IsFirstStep(&actor));
    actor.Update();
    EXPECT_EQ(NS::GetStateStep(&actor), 1u);
    actor.Update();
    EXPECT_EQ(NS::GetStateStep(&actor), 2u);
    EXPECT_TRUE(NS::SetState<OwnedState>(&actor));
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
    EXPECT_EQ(actor.GetStateMachine(), actor.machine);
}

TEST(ActorStateMachine, MachineIsListedAsTheStateMachineClassObj)
{
    const std::unique_ptr<PlayerActor> player = NS::CreateActor<PlayerActor>();
    EXPECT_EQ(CountClassObjKey(player.get(), "StateMachine"), 1);
    const NS::StateMachine* playerPart = NS::Cast<NS::StateMachine>(ListedClassObj(player.get(), "StateMachine"));
    ASSERT_NE(playerPart, nullptr);
    ASSERT_NE(player->GetStateMachine(), nullptr);
    EXPECT_EQ(playerPart->Machine(), player->GetStateMachine());

    const std::unique_ptr<GL::Level::PropActor> prop = NS::CreateActor<GL::Level::PropActor>();
    EXPECT_EQ(CountClassObjKey(prop.get(), "StateMachine"), 1);
    const NS::StateMachine* propPart = NS::Cast<NS::StateMachine>(ListedClassObj(prop.get(), "StateMachine"));
    ASSERT_NE(propPart, nullptr);
    ASSERT_NE(prop->GetStateMachine(), nullptr);
    EXPECT_EQ(propPart->Machine(), prop->GetStateMachine());
}

TEST(ActorStateMachine, SavedJsonGainsNoStateMachineFields)
{
    const std::unique_ptr<PlayerActor> player = NS::CreateActor<PlayerActor>();
    ExpectNoStateMachineFields(NS::ActorToJson(player.get()));
    ExpectNoStateMachineFields(*NS::ActorFileLibrary::Get().Values("PlayerActor"));

    const std::unique_ptr<GL::Level::PropActor> prop = NS::CreateActor<GL::Level::PropActor>();
    ExpectNoStateMachineFields(NS::ActorToJson(prop.get()));
    ExpectNoStateMachineFields(*NS::ActorFileLibrary::Get().Values("PropActor"));
}

TEST(ActorStateMachine, SecondBuildFailsAndKeepsTheFirstMachine)
{
    ProbeActor actor;
    ASSERT_TRUE(actor.BuildProbe());
    NS::StateMachineOf<ProbeActor>* first = actor.machine;

    const TestLogCapture log;
    EXPECT_FALSE(actor.BuildSecond());
    EXPECT_EQ(log.ErrorCount(), 1);

    EXPECT_EQ(actor.machine, first);
    EXPECT_EQ(actor.GetStateMachine(), first);
    EXPECT_TRUE(NS::IsState<ProbeState>(&actor));
}
