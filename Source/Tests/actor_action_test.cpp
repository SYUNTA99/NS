#include "NSlib/CoreObject/ClassObjs/ActionTimeline.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/ModelShake.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/StateMachineOf.h"
#include "TestActorFile.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// 動作のタイムラインのファイルの形・読み込みと保存の往復・壊れた動作の扱いと、動作を状態の歩で進める部品を縛る

namespace
{
    // 当たる直前の動作の形。縮みと自機以外の止めを、状態に入ってからの歩に置く
    NS::ActorAction MakeBeforeContactAction()
    {
        NS::HitShapeItem shape;
        shape.along.count = 2;
        shape.along.keys[0] = NS::Curve::Key{0.0f, 1.0f};
        shape.along.keys[1] = NS::Curve::Key{6.0f, 0.85f};
        NS::ActorAction action;
        action.name = "BeforeContactCenter";
        action.events = {
            {shape, 0, 7},
            {NS::HitOthersStopItem{}, 4, 2},
            {NS::HitFlashItem{}, 4, 0},
        };
        return action;
    }
} // namespace

// 事象は種類・始まりの歩・長さ・欄を持ち、並びの順のまま往復する
TEST(ActorAction, EventsRoundTripThroughTheFileFormInOrder)
{
    const NS::ActorAction written = MakeBeforeContactAction();
    const nlohmann::json doc = NS::ActorActionToJson(written);
    EXPECT_EQ(doc["name"].get<std::string>(), "BeforeContactCenter");
    ASSERT_EQ(doc["events"].size(), 3u);
    EXPECT_EQ(doc["events"][1]["type"].get<std::string>(), "OthersStop");
    EXPECT_EQ(doc["events"][1]["start"].get<int>(), 4);

    std::string error;
    const std::optional<NS::ActorAction> read = NS::ParseActorAction(doc, error);
    ASSERT_TRUE(read.has_value()) << error;
    // センサーの入り切りの無い動作は鍵を書かない
    EXPECT_FALSE(doc.contains("sensors"));
    EXPECT_TRUE(read->sensors.empty());
    ASSERT_EQ(read->events.size(), written.events.size());
    for (std::size_t i = 0; i < written.events.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(read->events[i].item.index(), written.events[i].item.index());
        EXPECT_EQ(read->events[i].start, written.events[i].start);
        EXPECT_EQ(read->events[i].length, written.events[i].length);
    }
    EXPECT_EQ(std::get<NS::HitShapeItem>(read->events[0].item).along,
              std::get<NS::HitShapeItem>(written.events[0].item).along);
    EXPECT_EQ(NS::ActorActionToJson(*read), doc);
}

// センサーの入り切りは、どのセンサーか・入れるか外すか・始まり・長さを持ち、並びの順のまま往復する
TEST(ActorAction, SensorSwitchesRoundTripThroughTheFileForm)
{
    NS::ActorAction written;
    written.name = "Swing";
    written.sensors = {{.attack = true, .valid = true, .start = 2, .length = 5},
                       {.attack = false, .valid = false, .start = 0, .length = 3}};
    const nlohmann::json doc = NS::ActorActionToJson(written);
    ASSERT_EQ(doc["sensors"].size(), 2u);
    EXPECT_TRUE(doc["sensors"][0]["attack"].get<bool>());
    EXPECT_EQ(doc["sensors"][0]["start"].get<int>(), 2);

    std::string error;
    const std::optional<NS::ActorAction> read = NS::ParseActorAction(doc, error);
    ASSERT_TRUE(read.has_value()) << error;
    ASSERT_EQ(read->sensors.size(), 2u);
    for (std::size_t i = 0; i < written.sensors.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(read->sensors[i].attack, written.sensors[i].attack);
        EXPECT_EQ(read->sensors[i].valid, written.sensors[i].valid);
        EXPECT_EQ(read->sensors[i].start, written.sensors[i].start);
        EXPECT_EQ(read->sensors[i].length, written.sensors[i].length);
    }
    EXPECT_EQ(NS::ActorActionToJson(*read), doc);
}

TEST(ActorAction, MissingFieldsKeepTheDefaultsAndUnknownFieldsAreSkipped)
{
    const nlohmann::json doc = nlohmann::json::parse(R"({"name": "A", "古い欄": 0, "events": [
        {"type": "Flash", "start": 1, "length": 6, "fields": {"濃さ": 0.3, "消した欄": 2}},
        {"type": "ZoomRoll", "start": 2, "length": 7}
    ], "sensors": [
        {"start": 1, "length": 2, "古い欄": 1}
    ]})");
    std::string error;
    const TestLogCapture log;
    const std::optional<NS::ActorAction> read = NS::ParseActorAction(doc, error);
    ASSERT_TRUE(read.has_value()) << error;
    ASSERT_EQ(read->events.size(), 2u);
    EXPECT_FLOAT_EQ(std::get<NS::HitFlashItem>(read->events[0].item).alpha, 0.3f);
    EXPECT_FLOAT_EQ(std::get<NS::HitZoomRollItem>(read->events[1].item).zoom, NS::HitZoomRollItem{}.zoom);
    ASSERT_EQ(read->sensors.size(), 1u);
    EXPECT_EQ(read->sensors[0].attack, NS::ActionSensorEvent{}.attack);
    EXPECT_EQ(read->sensors[0].valid, NS::ActionSensorEvent{}.valid);
    EXPECT_EQ(log.WarningCount(), 3);
    EXPECT_EQ(log.ErrorCount(), 0);
}

TEST(ActorAction, BrokenActionsAreRejectedWithAReason)
{
    const char* const broken[] = {
        R"({"events": []})",
        R"({"name": "", "events": []})",
        R"({"name": "A"})",
        R"({"name": "A", "events": [{"type": "Explode", "start": 0, "length": 1}]})",
        R"({"name": "A", "events": [{"type": "Flash", "start": 0, "length": -1}]})",
        // 時計は状態に入った歩を 0 と数えるので、負の始まりは来ない
        R"({"name": "A", "events": [{"type": "Flash", "start": -1, "length": 1}]})",
        R"({"name": "A", "events": [{"type": "Flash", "start": 1.5, "length": 1}]})",
        R"({"name": "A", "events": [{"type": "Flash", "length": 1}]})",
        R"({"name": "A", "events": [{"type": "Flash", "start": 0}]})",
        R"({"name": "A", "events": [], "sensors": {}})",
        R"({"name": "A", "events": [], "sensors": [{"start": 0, "length": -1}]})",
        R"({"name": "A", "events": [], "sensors": [{"length": 1}]})",
        R"({"name": "A", "events": [], "sensors": [{"start": 0, "length": 1, "attack": 1}]})",
        R"({"name": "A", "events": [], "sensors": [{"start": 0, "length": 1, "valid": "on"}]})",
        R"({"name": "A", "events": [], "sensors": ["Attack"]})",
        R"("A")",
    };
    for (const char* text : broken)
    {
        SCOPED_TRACE(text);
        std::string error;
        EXPECT_FALSE(NS::ParseActorAction(nlohmann::json::parse(text), error).has_value());
        EXPECT_FALSE(error.empty());
    }
}

// .actor の動作の並びは、壊れた動作と名前の重なった動作を ERROR で知らせて読まず、残りを読む
TEST(ActorAction, ActorFileSkipsBrokenAndDuplicateActions)
{
    nlohmann::json actorFile = ClassOnlyActorFile("PlayerActor");
    NS::SetActorFileActions(actorFile, nlohmann::json::parse(R"([
        {"name": "BeforeContact", "events": []},
        {"name": "Broken", "events": [{"type": "Flash", "start": -1, "length": 1}]},
        {"name": "BeforeContact", "events": [{"type": "Flash", "start": 0, "length": 1}]},
        {"name": "Hit", "events": []}
    ])"));
    const TestLogCapture log;
    const std::vector<NS::ActorAction> actions = NS::ReadActorActions("PlayerActor", actorFile);
    ASSERT_EQ(actions.size(), 2u);
    EXPECT_EQ(actions[0].name, "BeforeContact");
    EXPECT_TRUE(actions[0].events.empty());
    EXPECT_EQ(actions[1].name, "Hit");
    EXPECT_EQ(log.ErrorCount(), 2);
}

// 動作が 1 つも無い時は鍵を書かない。束と動作を持たない .actor の形を変えない
TEST(ActorAction, NoActionsWriteNoKey)
{
    nlohmann::json actorFile = ClassOnlyActorFile("PlayerActor");
    NS::WriteActorActions(actorFile, {MakeBeforeContactAction()});
    ASSERT_NE(NS::ActorFileActions(actorFile), nullptr);
    NS::WriteActorActions(actorFile, {});
    NS::WriteHitReactionSets(actorFile, {});
    EXPECT_EQ(actorFile, ClassOnlyActorFile("PlayerActor"));
}

// 動作のタイムラインの部品。状態に入る時に動作を始め、状態の歩を時計にして事象を起こす

namespace
{
    class ActionProbeActor;

    // 何もしない最初の状態
    class ActionIdleState final : public NS::StateOf<ActionIdleState, ActionProbeActor>
    {
    public:
        void OnStep(ActionProbeActor&, float) override {}
    };

    // 入る時に持ち主の actionName の動作を startFrame から始める状態
    class ActionRunState final : public NS::StateOf<ActionRunState, ActionProbeActor>
    {
    public:
        void OnEnter(ActionProbeActor& owner) override;
        void OnStep(ActionProbeActor&, float) override {}
    };

    // 体と攻撃のセンサー・当たりの返り・動作のタイムラインと、2 つの状態を持つアクター
    class ActionProbeActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_NONE(ActionProbeActor, NS::SceneActor)

        std::string actionName;
        int startFrame = 0;
        bool started = false;
        NS::SceneActor* partner = nullptr;

    protected:
        void OnInit() override
        {
            NS::ShapeHitSensor* body = CreateClassObj<NS::HitSensor, NS::ShapeHitSensor>(m_bodySensor, this);
            body->SetSphere(0.5f);
            NS::ShapeHitSensor* attack = CreateClassObj<NS::HitSensor, NS::ShapeHitSensor>(m_attackSensor, this);
            attack->SetSphere(0.8f);
            CreateClassObj(m_model, this);
            CreateClassObj(m_hitReaction, this);
            CreateClassObj(m_actionTimeline, this);
            NS::StateMachineOf<ActionProbeActor>* machine = nullptr;
            (void)BuildStateMachine<ActionProbeActor, ActionIdleState, ActionRunState>(*this, machine);
        }
    };

    void ActionRunState::OnEnter(ActionProbeActor& owner)
    {
        owner.started = owner.ActionTimelineClassObj()->Start(owner.actionName,
                                                               owner.startFrame,
                                                               owner.Root().Position(),
                                                               owner.BodySensorClassObj(),
                                                               owner.partner->BodySensorClassObj());
    }
} // namespace

NS_PLACEABLE(ActionProbeActor, "試しの動作のタイムライン")

namespace
{
    constexpr const char* k_ActionProbeName = "ActionProbeActor";

    // 名前 name で、長さの長い持ち主へ渡す項目。始まった物が OwnerRuns に残る
    NS::ActorActionEvent OwnerEvent(std::string name, int start)
    {
        NS::HitOwnerItem item;
        item.name = std::move(name);
        return NS::ActorActionEvent{.item = item, .start = start, .length = 100};
    }

    // actions を書いた .actor を敷いて、原点のアクターと前の相手を置いた場面
    struct ActionScene
    {
        NS::Scene scene;
        ActionProbeActor* self = nullptr;
        ActionProbeActor* other = nullptr;

        explicit ActionScene(const std::vector<NS::ActorAction>& actions)
        {
            nlohmann::json actorFile = ClassOnlyActorFile(k_ActionProbeName);
            NS::WriteActorActions(actorFile, actions);
            const ScopedActorFile scoped(k_ActionProbeName, std::move(actorFile));
            self = scene.SpawnObject<ActionProbeActor>("Self");
            other = scene.SpawnObject<ActionProbeActor>("Other");
            if (self != nullptr && other != nullptr)
            {
                other->Root().SetPosition(NS::Vector3{0.0f, 0.0f, 3.0f});
                self->partner = other;
            }
        }

        // self の状態を動作の状態へ移す予約をする。次の Step の頭で入る
        void Enter(std::string name, int startFrame = 0)
        {
            self->actionName = std::move(name);
            self->startFrame = startFrame;
            (void)NS::SetState<ActionRunState>(self);
        }

        void Step() { scene.ExecutePhase(NS::UpdatePhase::Triggers); }

        // self の持ち主へ渡す項目の名前。始まった順
        [[nodiscard]] std::vector<std::string> Started() const
        {
            std::vector<std::string> names;
            for (const NS::HitOwnerRun& run : self->HitReactionClassObj()->OwnerRuns())
            {
                names.push_back(run.item.name);
            }
            return names;
        }
    };

    using Names = std::vector<std::string>;
    using Indices = std::vector<std::size_t>;
} // namespace

// 事象は状態に入ってからの歩で起き、同じ歩の事象は並びの順に起きる。始まった事象は記録から読める
TEST(ActionTimeline, EventsStartOnTheStateStepsInFileOrder)
{
    NS::ActorAction action;
    action.name = "Run";
    action.events = {OwnerEvent("A", 0), OwnerEvent("Z", 2), OwnerEvent("Y", 2), OwnerEvent("D", 4)};
    ActionScene probes({action});
    ASSERT_NE(probes.self, nullptr);
    const NS::ActionTimeline& timeline = *probes.self->ActionTimelineClassObj();
    probes.Enter("Run");

    const Names expected[] = {{"A"}, {"A"}, {"A", "Z", "Y"}, {"A", "Z", "Y"}, {"A", "Z", "Y", "D"}};
    const Indices started[] = {{0}, {}, {1, 2}, {}, {3}};
    for (int frame = 0; frame < 5; ++frame)
    {
        SCOPED_TRACE(frame);
        probes.Step();
        EXPECT_EQ(probes.Started(), expected[frame]);
        EXPECT_EQ(timeline.StartedEvents(), started[frame]);
        EXPECT_EQ(timeline.Frame(), frame);
    }
    EXPECT_TRUE(probes.self->started);
    EXPECT_EQ(timeline.CurrentName(), "Run");
}

// 止めの間は状態の歩が進まないので、動作の時計も進まない
TEST(ActionTimeline, TheClockHoldsDuringTheHitStop)
{
    NS::ActorAction action;
    action.name = "Run";
    action.events = {OwnerEvent("A", 0), OwnerEvent("B", 1)};
    ActionScene probes({action});
    ASSERT_NE(probes.self, nullptr);
    probes.Enter("Run");
    probes.Step();
    ASSERT_EQ(probes.Started(), Names{"A"});
    probes.self->RequestHitStop(3);
    for (int frame = 0; frame < 3; ++frame)
    {
        SCOPED_TRACE(frame);
        probes.Step();
        EXPECT_EQ(probes.Started(), Names{"A"});
        EXPECT_EQ(probes.self->ActionTimelineClassObj()->Frame(), 0);
    }
    probes.Step();
    EXPECT_EQ(probes.Started(), (Names{"A", "B"}));
}

// 状態が移ると、前の動作の残りの事象は起きない
TEST(ActionTimeline, LeavingTheStateDropsTheRestOfTheAction)
{
    NS::ActorAction action;
    action.name = "Run";
    action.events = {OwnerEvent("A", 0), OwnerEvent("B", 3)};
    ActionScene probes({action});
    ASSERT_NE(probes.self, nullptr);
    probes.Enter("Run");
    probes.Step();
    probes.Step();
    (void)NS::SetState<ActionIdleState>(probes.self);
    for (int frame = 0; frame < 4; ++frame)
    {
        SCOPED_TRACE(frame);
        probes.Step();
        EXPECT_EQ(probes.Started(), Names{"A"});
        EXPECT_FALSE(probes.self->ActionTimelineClassObj()->IsRunning());
    }
}

// 始める歩を渡すと、その歩から始まり、それより前の事象は起きない
TEST(ActionTimeline, AStartFrameSkipsTheEarlierEvents)
{
    NS::ActorAction action;
    action.name = "Run";
    action.events = {OwnerEvent("Early", 1), OwnerEvent("At", 3), OwnerEvent("Late", 5)};
    ActionScene probes({action});
    ASSERT_NE(probes.self, nullptr);
    probes.Enter("Run", 3);
    probes.Step();
    EXPECT_EQ(probes.Started(), Names{"At"});
    EXPECT_EQ(probes.self->ActionTimelineClassObj()->Frame(), 3);
    probes.Step();
    EXPECT_EQ(probes.Started(), Names{"At"});
    probes.Step();
    EXPECT_EQ(probes.Started(), (Names{"At", "Late"}));
}

// 始める歩の途中にある形は、その歩の姿から描く。形は動作のフレームに合わせて描く姿で、始まりの歩に起こす事象と違う
TEST(ActionTimeline, AStartFrameDrawsTheShapeFromThatFrame)
{
    NS::HitShapeItem shape;
    shape.height.count = 2;
    shape.height.keys[0] = NS::Curve::Key{0.0f, 1.0f};
    shape.height.keys[1] = NS::Curve::Key{6.0f, 0.4f};
    NS::ActorAction action;
    action.name = "Run";
    action.events = {{shape, 0, 7}};
    ActionScene probes({action});
    ASSERT_NE(probes.self, nullptr);
    const NS::DrawShapeDesc drawn{.height = shape.height, .alongDirection = NS::Vector3{0.0f, 0.0f, 1.0f}, .length = 7};
    probes.Enter("Run", 3);
    for (int frame = 3; frame < 7; ++frame)
    {
        SCOPED_TRACE(frame);
        probes.Step();
        EXPECT_NEAR(probes.self->ModelClassObj()->GetDrawScale().y, drawn.ScaleAt(frame).y, 1.0e-6f);
    }
    probes.Step();
    EXPECT_FLOAT_EQ(probes.self->ModelClassObj()->GetDrawScale().y, 1.0f);
}

// センサーの入り切りは始まりの歩に効き、終わりの歩に戻る。途中で状態を出ても戻る
TEST(ActionTimeline, SensorSwitchesHoldForTheirLength)
{
    NS::ActorAction flicker;
    flicker.name = "Flicker";
    flicker.sensors = {{.attack = false, .valid = false, .start = 1, .length = 2}};
    NS::ActorAction hold;
    hold.name = "Hold";
    hold.sensors = {{.attack = true, .valid = false, .start = 0, .length = 10}};
    ActionScene probes({flicker, hold});
    ASSERT_NE(probes.self, nullptr);
    const NS::HitSensor& body = *probes.self->BodySensorClassObj();
    const NS::HitSensor& attack = *probes.self->AttackSensorClassObj();

    probes.Enter("Flicker");
    const bool bodyValid[] = {true, false, false, true, true};
    for (int frame = 0; frame < 5; ++frame)
    {
        SCOPED_TRACE(frame);
        probes.Step();
        EXPECT_EQ(body.IsValid(), bodyValid[frame]);
        EXPECT_TRUE(attack.IsValid());
    }

    (void)NS::SetState<ActionIdleState>(probes.self);
    probes.Step();
    probes.Enter("Hold");
    probes.Step();
    EXPECT_FALSE(attack.IsValid());
    EXPECT_TRUE(body.IsValid());
    (void)NS::SetState<ActionIdleState>(probes.self);
    probes.Step();
    EXPECT_TRUE(attack.IsValid());
}

// 名前の無い動作は始めず、知らせる
TEST(ActionTimeline, AMissingActionIsNotStarted)
{
    ActionScene probes({});
    ASSERT_NE(probes.self, nullptr);
    const TestLogCapture log;
    probes.Enter("Nothing");
    probes.Step();
    EXPECT_FALSE(probes.self->started);
    EXPECT_FALSE(probes.self->ActionTimelineClassObj()->IsRunning());
    EXPECT_EQ(log.WarningCount(), 1);
}
