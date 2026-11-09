#include "Game/Level/SensorKinds.h"
#include "NSlib/Core/Animation/AnimClip.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/CoreObject/Action/ActorAction.h"
#include "NSlib/CoreObject/ClassObjs/Animation.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/StateMachineOf.h"
#include "NSlib/CoreObject/System/ScreenFlashDirector.h"
#include "TestActorFile.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// 動作のタイムラインのファイルの形・読み込みと保存の往復・壊れた動作の扱いと、動作を状態の歩で進める部品を縛る

namespace
{
    // 長さだけを持つクリップ。動作の時計はクリップの名前と骨を見ない
    NS::Anim::AnimClip ClipOf(float duration)
    {
        NS::Anim::AnimClip clip;
        clip.duration = duration;
        return clip;
    }

    // 当たる直前の動作の形。振動とカメラの揺れと画面の光を、状態に入ってからの歩に置く
    NS::ActorActionInfo MakeBeforeContactAction()
    {
        NS::ActionPadInfo pad;
        pad.left.count = 2;
        pad.left.keys[0] = NS::Curve::Key{0.0f, 1.0f};
        pad.left.keys[1] = NS::Curve::Key{6.0f, 0.85f};
        NS::ActorActionInfo action;
        action.name = "BeforeContactCenter";
        action.events = {
            {pad, 0, 7},
            {NS::ActionCameraInfo{}, 4, 2},
            {NS::ActionScreenInfo{}, 4, 0},
        };
        return action;
    }
} // namespace

// 事象は種類・始まりの歩・長さ・欄を持ち、並びの順のまま往復する
TEST(ActorActionInfo, EventsRoundTripThroughTheFileFormInOrder)
{
    const NS::ActorActionInfo written = MakeBeforeContactAction();
    const nlohmann::json doc = NS::ActorActionInfoToJson(written);
    EXPECT_EQ(doc["name"].get<std::string>(), "BeforeContactCenter");
    ASSERT_EQ(doc["events"].size(), 3u);
    EXPECT_EQ(doc["events"][1]["type"].get<std::string>(), "Camera");
    EXPECT_EQ(doc["events"][1]["start"].get<int>(), 4);

    std::string error;
    const std::optional<NS::ActorActionInfo> read = NS::ParseActorActionInfo(doc, error);
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
    EXPECT_EQ(std::get<NS::ActionPadInfo>(read->events[0].item).left,
              std::get<NS::ActionPadInfo>(written.events[0].item).left);
    EXPECT_EQ(NS::ActorActionInfoToJson(*read), doc);
}

// 動作の項目は 6 つの種類 (アニメーション・効果音・エフェクト・振動・カメラ・画面) で、種類の名前と欄が往復する
TEST(ActorActionInfo, EachActionItemKindRoundTripsThroughTheFileForm)
{
    NS::ActionAnimInfo anim;
    anim.blendSeconds = 0.2f;
    anim.looping = false;
    NS::ActionSoundInfo sound;
    sound.sound.name = "Player/Jump";
    sound.volume = 0.5f;
    NS::ActionPadInfo pad;
    pad.left.count = 1;
    pad.left.keys[0] = NS::Curve::Key{0.0f, 0.7f};
    NS::ActionCameraInfo camera;
    camera.trauma = 0.4f;
    NS::ActionScreenInfo screen;
    screen.alpha = 0.3f;
    NS::ActorActionInfo written;
    written.name = "Jump";
    written.events = {
        {anim, 0, 10}, {sound, 0, 1}, {NS::ActionEffectInfo{}, 1, 1}, {pad, 2, 4}, {camera, 3, 5}, {screen, 4, 2}};

    const nlohmann::json doc = NS::ActorActionInfoToJson(written);
    const char* const typeNames[] = {"Anim", "Sound", "Effect", "Pad", "Camera", "Screen"};
    ASSERT_EQ(doc["events"].size(), std::size(typeNames));
    for (std::size_t i = 0; i < std::size(typeNames); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(doc["events"][i]["type"].get<std::string>(), typeNames[i]);
    }

    std::string error;
    const std::optional<NS::ActorActionInfo> read = NS::ParseActorActionInfo(doc, error);
    ASSERT_TRUE(read.has_value()) << error;
    EXPECT_FLOAT_EQ(std::get<NS::ActionAnimInfo>(read->events[0].item).blendSeconds, 0.2f);
    EXPECT_FALSE(std::get<NS::ActionAnimInfo>(read->events[0].item).looping);
    EXPECT_EQ(std::get<NS::ActionSoundInfo>(read->events[1].item).sound.name, "Player/Jump");
    EXPECT_FLOAT_EQ(std::get<NS::ActionSoundInfo>(read->events[1].item).volume, 0.5f);
    EXPECT_EQ(std::get<NS::ActionPadInfo>(read->events[3].item).left, pad.left);
    EXPECT_FLOAT_EQ(std::get<NS::ActionCameraInfo>(read->events[4].item).trauma, 0.4f);
    EXPECT_FLOAT_EQ(std::get<NS::ActionScreenInfo>(read->events[5].item).alpha, 0.3f);
    EXPECT_EQ(NS::ActorActionInfoToJson(*read), doc);
}

// 当たりの反応の種類 (白い光・持ち主以外の止め・形・持ち主へ渡す項目) は動作の項目ではないので、動作ごと読まない
TEST(ActorActionInfo, HitReactionItemKindsAreNotActionItems)
{
    for (const char* type : {"Flash", "OthersStop", "Shape", "Owner"})
    {
        SCOPED_TRACE(type);
        nlohmann::json doc = nlohmann::json{{"name", "A"}};
        doc["events"] = nlohmann::json::array({nlohmann::json{{"type", type}, {"start", 0}, {"length", 1}}});
        std::string error;
        EXPECT_FALSE(NS::ParseActorActionInfo(doc, error).has_value());
        EXPECT_NE(error.find(type), std::string::npos);
    }
}

// センサーの入り切りは、どのセンサーか・入れるか外すか・始まり・長さを持ち、並びの順のまま往復する
TEST(ActorActionInfo, SensorSwitchesRoundTripThroughTheFileForm)
{
    NS::ActorActionInfo written;
    written.name = "Swing";
    written.sensors = {{.sensor = "Attack", .valid = true, .start = 2, .length = 5},
                       {.sensor = "Body", .valid = false, .start = 0, .length = 3}};
    const nlohmann::json doc = NS::ActorActionInfoToJson(written);
    ASSERT_EQ(doc["sensors"].size(), 2u);
    EXPECT_EQ(doc["sensors"][0]["sensor"].get<std::string>(), "Attack");
    EXPECT_EQ(doc["sensors"][0]["start"].get<int>(), 2);

    std::string error;
    const std::optional<NS::ActorActionInfo> read = NS::ParseActorActionInfo(doc, error);
    ASSERT_TRUE(read.has_value()) << error;
    ASSERT_EQ(read->sensors.size(), 2u);
    for (std::size_t i = 0; i < written.sensors.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(read->sensors[i].sensor, written.sensors[i].sensor);
        EXPECT_EQ(read->sensors[i].valid, written.sensors[i].valid);
        EXPECT_EQ(read->sensors[i].start, written.sensors[i].start);
        EXPECT_EQ(read->sensors[i].length, written.sensors[i].length);
    }
    EXPECT_EQ(NS::ActorActionInfoToJson(*read), doc);
}

// 動作の長さ (状態の歩の回数) は往復する。鍵が無い動作は長さを決めておらず、決めていない長さは鍵を書かない
TEST(ActorActionInfo, TheActionLengthRoundTripsAndIsUnsetWhenMissing)
{
    const TestLogCapture log;
    NS::ActorActionInfo written;
    written.name = "AirDash";
    written.length = 29;
    const nlohmann::json doc = NS::ActorActionInfoToJson(written);
    ASSERT_TRUE(doc.contains("length"));
    EXPECT_EQ(doc.at("length").get<int>(), 29);
    std::string error;
    const std::optional<NS::ActorActionInfo> read = NS::ParseActorActionInfo(doc, error);
    ASSERT_TRUE(read.has_value()) << error;
    EXPECT_EQ(read->length, 29);
    EXPECT_EQ(NS::ActorActionInfoToJson(*read), doc);

    const std::optional<NS::ActorActionInfo> missing =
        NS::ParseActorActionInfo(nlohmann::json::parse(R"({"name": "A", "events": []})"), error);
    ASSERT_TRUE(missing.has_value()) << error;
    EXPECT_EQ(missing->length, 0);
    EXPECT_FALSE(NS::ActorActionInfoToJson(*missing).contains("length"));
    EXPECT_EQ(log.WarningCount(), 0);
}

TEST(ActorActionInfo, MissingFieldsKeepTheDefaultsAndUnknownFieldsAreSkipped)
{
    const nlohmann::json doc = nlohmann::json::parse(R"({"name": "A", "古い欄": 0, "events": [
        {"type": "Screen", "start": 1, "length": 6, "fields": {"濃さ": 0.3, "消した欄": 2}},
        {"type": "Camera", "start": 2, "length": 7}
    ], "sensors": [
        {"sensor": "Body", "start": 1, "length": 2, "古い欄": 1}
    ]})");
    std::string error;
    const TestLogCapture log;
    const std::optional<NS::ActorActionInfo> read = NS::ParseActorActionInfo(doc, error);
    ASSERT_TRUE(read.has_value()) << error;
    ASSERT_EQ(read->events.size(), 2u);
    EXPECT_FLOAT_EQ(std::get<NS::ActionScreenInfo>(read->events[0].item).alpha, 0.3f);
    EXPECT_FLOAT_EQ(std::get<NS::ActionCameraInfo>(read->events[1].item).trauma, NS::ActionCameraInfo{}.trauma);
    ASSERT_EQ(read->sensors.size(), 1u);
    EXPECT_EQ(read->sensors[0].sensor, "Body");
    EXPECT_EQ(read->sensors[0].valid, NS::ActionSensorEvent{}.valid);
    EXPECT_EQ(log.WarningCount(), 3);
    EXPECT_EQ(log.ErrorCount(), 0);
}

// 入り切りはセンサーを名前で指す。名前の無い物と空の名前は、どのセンサーか決まらないので読まない
TEST(ActorActionInfo, SensorSwitchWithoutANameIsRefused)
{
    for (const char* row : {R"({"start": 0, "length": 2})",
                            R"({"sensor": "", "start": 0, "length": 2})",
                            R"({"sensor": true, "start": 0, "length": 2})"})
    {
        SCOPED_TRACE(row);
        nlohmann::json doc = nlohmann::json{{"name", "A"}, {"events", nlohmann::json::array()}};
        doc["sensors"] = nlohmann::json::array({nlohmann::json::parse(row)});
        std::string error;
        EXPECT_FALSE(NS::ParseActorActionInfo(doc, error).has_value());
        EXPECT_NE(error.find("sensor"), std::string::npos);
    }
}

TEST(ActorActionInfo, BrokenActionsAreRejectedWithAReason)
{
    const char* const broken[] = {
        R"({"events": []})",
        R"({"name": "", "events": []})",
        R"({"name": "A"})",
        R"({"name": "A", "events": [{"type": "Explode", "start": 0, "length": 1}]})",
        R"({"name": "A", "events": [{"type": "Screen", "start": 0, "length": -1}]})",
        // 時計は状態に入った歩を 0 と数えるので、負の始まりは来ない
        R"({"name": "A", "events": [{"type": "Screen", "start": -1, "length": 1}]})",
        R"({"name": "A", "events": [{"type": "Screen", "start": 1.5, "length": 1}]})",
        R"({"name": "A", "events": [{"type": "Screen", "length": 1}]})",
        R"({"name": "A", "events": [{"type": "Screen", "start": 0}]})",
        R"({"name": "A", "events": [], "sensors": {}})",
        R"({"name": "A", "events": [], "sensors": [{"start": 0, "length": -1}]})",
        R"({"name": "A", "events": [], "sensors": [{"length": 1}]})",
        R"({"name": "A", "events": [], "sensors": [{"sensor": 1, "start": 0, "length": 1}]})",
        R"({"name": "A", "events": [], "sensors": [{"sensor": "Body", "start": 0, "length": 1, "valid": "on"}]})",
        R"({"name": "A", "events": [], "sensors": ["Attack"]})",
        R"({"name": "A", "events": [], "length": -1})",
        R"({"name": "A", "events": [], "length": 1.5})",
        R"("A")",
    };
    for (const char* text : broken)
    {
        SCOPED_TRACE(text);
        std::string error;
        EXPECT_FALSE(NS::ParseActorActionInfo(nlohmann::json::parse(text), error).has_value());
        EXPECT_FALSE(error.empty());
    }
}

// .actor の動作の並びは、壊れた動作と名前の重なった動作を ERROR で知らせて読まず、残りを読む
TEST(ActorActionInfo, ActorFileSkipsBrokenAndDuplicateActions)
{
    nlohmann::json actorFile = CodeValuesActorFile("PlayerActor");
    NS::SetActorFileActions(actorFile, nlohmann::json::parse(R"([
        {"name": "BeforeContact", "events": []},
        {"name": "Broken", "events": [{"type": "Screen", "start": -1, "length": 1}]},
        {"name": "BeforeContact", "events": [{"type": "Screen", "start": 0, "length": 1}]},
        {"name": "Hit", "events": []}
    ])"));
    const TestLogCapture log;
    const std::vector<NS::ActorActionInfo> actions = NS::ReadActorActions("PlayerActor", actorFile);
    ASSERT_EQ(actions.size(), 2u);
    EXPECT_EQ(actions[0].name, "BeforeContact");
    EXPECT_TRUE(actions[0].events.empty());
    EXPECT_EQ(actions[1].name, "Hit");
    EXPECT_EQ(log.ErrorCount(), 2);
}

// 読めた動作を直して書き戻しても、読めなかった動作と名前の重なった動作は .actor の同じ位置に元の字面のまま残る
TEST(ActorActionInfo, WritingBackKeepsTheActionsThatWereNotRead)
{
    const nlohmann::json original = nlohmann::json::parse(R"([
        {"name": "BeforeContact", "events": []},
        {"name": "Broken", "events": [{"type": "Screen", "start": -1, "length": 1}]},
        {"name": "BeforeContact", "events": [{"type": "Screen", "start": 0, "length": 1}]},
        {"name": "Hit", "events": []}
    ])");
    nlohmann::json actorFile = CodeValuesActorFile("PlayerActor");
    NS::SetActorFileActions(actorFile, original);
    const TestLogCapture log;
    std::vector<NS::ActorActionInfo> actions = NS::ReadActorActions("PlayerActor", actorFile);
    ASSERT_EQ(actions.size(), 2u);
    actions[1].events.push_back(NS::ActorActionEvent{.item = NS::ActionScreenInfo{}, .start = 2, .length = 3});
    NS::WriteActorActions(actorFile, actions);
    const nlohmann::json* list = NS::ActorFileActions(actorFile);
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->size(), 4u);
    EXPECT_EQ((*list)[0], original[0]);
    EXPECT_EQ((*list)[1], original[1]);
    EXPECT_EQ((*list)[2], original[2]);
    EXPECT_EQ((*list)[3]["name"], "Hit");
    ASSERT_EQ((*list)[3]["events"].size(), 1u);
    EXPECT_EQ((*list)[3]["events"][0]["start"], 2);
}

// 動作の長さは、始まりと長さを足すと int を越える事象でも負に回らない
TEST(ActorActionInfo, TheLengthDoesNotWrapPastInt)
{
    NS::ActorActionInfo action;
    action.events.push_back(
        NS::ActorActionEvent{.item = NS::ActionScreenInfo{}, .start = std::numeric_limits<int>::max(), .length = 1});
    EXPECT_EQ(NS::ActionLength(action), std::numeric_limits<int>::max());
}

// 動作が 1 つも無い時は鍵を書かない。束と動作を持たない .actor の形を変えない
TEST(ActorActionInfo, NoActionsWriteNoKey)
{
    nlohmann::json actorFile = CodeValuesActorFile("PlayerActor");
    NS::WriteActorActions(actorFile, {MakeBeforeContactAction()});
    ASSERT_NE(NS::ActorFileActions(actorFile), nullptr);
    NS::WriteActorActions(actorFile, {});
    NS::WriteHitReactionSets(actorFile, {});
    EXPECT_EQ(actorFile, CodeValuesActorFile("PlayerActor"));
}

// 動作の部品。状態に入る時に名前で動作を始め、状態の歩かアニメーションの時刻を時計にして、始まった項目を持ち主が自分の部品へ頼む

namespace
{
    class ActionProbeActor;

    // 何もしない最初の状態
    class ActionIdleState final : public NS::StateOf<ActionIdleState, ActionProbeActor>
    {
    public:
        void OnStep(ActionProbeActor* owner, float) override
        {
            NS_ASSERT(Game, owner != nullptr, "状態の持ち主が無い");
            (void)owner;
        }
    };

    // 入る時に持ち主の actionName の動作を始める状態
    class ActionRunState final : public NS::StateOf<ActionRunState, ActionProbeActor>
    {
    public:
        void OnEnter(ActionProbeActor* owner) override;
        void OnStep(ActionProbeActor* owner, float) override;
    };

    // 体と攻撃のセンサー・動作の部品と、2 つの状態を持つアクター
    class ActionProbeActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_NONE(ActionProbeActor, NS::SceneActor)

        std::string actionName;
        bool started = false;
        std::vector<bool> endSeen; // 動作の状態の OnStep ごとに、IsStateEnd が答えた値
        bool startOnEnter = true;  // 偽なら、動作の状態に入っても動作を始めない

    protected:
        void OnInit() override
        {
            CreateClassObj(m_hitSensors);
            m_bodySensor =
                m_hitSensors->AddShape(this, "Body", GL::Level::SensorKind::Unset, NS::HitSensorShape::Sphere);
            m_attackSensor =
                m_hitSensors->AddShape(this, "Attack", GL::Level::SensorKind::Unset, NS::HitSensorShape::Sphere);
            NS::Cast<NS::ShapeHitSensor>(m_bodySensor)->SetSphere(0.5f);
            NS::Cast<NS::ShapeHitSensor>(m_attackSensor)->SetSphere(0.8f);
            CreateClassObj(m_actorAction);
            NS::StateMachineOf<ActionProbeActor>* machine = nullptr;
            (void)BuildStateMachine<ActionProbeActor, ActionIdleState, ActionRunState>(this, machine);
        }

    private:
        NS::HitSensor* m_bodySensor = nullptr;
        NS::HitSensor* m_attackSensor = nullptr;
    };

    void ActionRunState::OnEnter(ActionProbeActor* owner)
    {
        NS_ASSERT(Game, owner != nullptr, "状態の持ち主が無い");
        if (!owner->startOnEnter)
        {
            return;
        }
        owner->started = owner->StartStateAction(owner->actionName);
    }

    void ActionRunState::OnStep(ActionProbeActor* owner, float)
    {
        NS_ASSERT(Game, owner != nullptr, "状態の持ち主が無い");
        owner->endSeen.push_back(NS::IsStateEnd(owner));
    }
} // namespace

NS_PLACEABLE(ActionProbeActor, "試しの動作")

namespace
{
    constexpr const char* k_ActionProbeName = "ActionProbeActor";

    // 名前 name の効果音を鳴らす項目。音の部品を持たない試しのアクターでは鳴らず、始まった事だけが残る
    NS::ActorActionEvent SoundEvent(std::string name, int start)
    {
        NS::ActionSoundInfo item;
        item.sound.name = std::move(name);
        return NS::ActorActionEvent{.item = item, .start = start, .length = 1};
    }

    // actions を書いた .actor を敷いて、原点にアクターを置いた場面
    struct ActionScene
    {
        NS::Scene scene;
        ActionProbeActor* self = nullptr;
        std::vector<std::string> started; // 始まった効果音の項目の名前。始まった順
        // 前の Step で読んだ動作とフレーム。ヒットストップの間は Advance が呼ばれず、StartedEvents
        // が前のフレームのまま残る
        const NS::ActorActionInfo* readAction = nullptr;
        int readFrame = -1;

        explicit ActionScene(const std::vector<NS::ActorActionInfo>& actions)
        {
            nlohmann::json actorFile = CodeValuesActorFile(k_ActionProbeName);
            NS::WriteActorActions(actorFile, actions);
            const ScopedActorFile scoped(k_ActionProbeName, std::move(actorFile));
            self = scene.SpawnActor<ActionProbeActor>("Self");
        }

        // self の状態を動作の状態へ移す予約をする。次の Step の頭で入る
        void Enter(std::string name)
        {
            self->actionName = std::move(name);
            (void)NS::SetState<ActionRunState>(self);
        }

        // 1 フレーム進め、その歩に始まった効果音の項目の名前を started へ足す
        void Step()
        {
            scene.OnUpdate();
            const NS::ActorAction& actions = *self->ActorActionClassObj();
            const NS::ActorActionInfo* action = actions.CurrentAction();
            if (action == nullptr || (action == readAction && actions.Frame() == readFrame))
            {
                return;
            }
            readAction = action;
            readFrame = actions.Frame();
            for (const std::size_t index : actions.StartedEvents())
            {
                if (const NS::ActionSoundInfo* sound = std::get_if<NS::ActionSoundInfo>(&action->events[index].item))
                {
                    started.push_back(sound->sound.name);
                }
            }
        }

        [[nodiscard]] const std::vector<std::string>& Started() const noexcept { return started; }
    };

    using Names = std::vector<std::string>;
    using Indices = std::vector<std::size_t>;
} // namespace

// 事象は状態に入ってからの歩で起き、同じ歩の事象は並びの順に起きる。始まった事象は記録から読める
TEST(ActorAction, EventsStartOnTheStateStepsInFileOrder)
{
    NS::ActorActionInfo action;
    action.name = "Run";
    action.events = {SoundEvent("A", 0), SoundEvent("Z", 2), SoundEvent("Y", 2), SoundEvent("D", 4)};
    ActionScene probes({action});
    ASSERT_NE(probes.self, nullptr);
    const NS::ActorAction& actions = *probes.self->ActorActionClassObj();
    probes.Enter("Run");

    const Names expected[] = {{"A"}, {"A"}, {"A", "Z", "Y"}, {"A", "Z", "Y"}, {"A", "Z", "Y", "D"}};
    const Indices started[] = {{0}, {}, {1, 2}, {}, {3}};
    for (int frame = 0; frame < 5; ++frame)
    {
        SCOPED_TRACE(frame);
        probes.Step();
        EXPECT_EQ(probes.Started(), expected[frame]);
        EXPECT_EQ(actions.StartedEvents(), started[frame]);
        EXPECT_EQ(actions.Frame(), frame);
    }
    EXPECT_TRUE(probes.self->started);
    EXPECT_EQ(actions.CurrentName(), "Run");
}

// 状態に結ばずに始めた動作は、フレーム 0 の後に結んだクリップの時刻で進む。1 回に 2 フレーム以上進むと、
// 飛んだフレームの事象も起こす
TEST(ActorAction, AnActionWithoutAStateFollowsTheBoundClipTime)
{
    NS::ActorActionInfo action;
    action.name = "Wave";
    action.events = {SoundEvent("A", 0), SoundEvent("B", 1), SoundEvent("C", 2), SoundEvent("D", 4)};
    ActionScene probes({action});
    ASSERT_NE(probes.self, nullptr);
    NS::ActorAction& actions = *probes.self->ActorActionClassObj();
    const float step = NS::OS::FrameTimer::FixedDelta();
    const NS::Anim::AnimClip clip = ClipOf(step * 30.0f);
    ASSERT_TRUE(actions.Start("Wave"));

    actions.Advance(NS::ActionClock{});
    EXPECT_EQ(actions.StartedEvents(), Indices{0});
    actions.BindClip(NS::ActionClock{.clip = &clip});
    // クリップの時刻が進まなければ、事象は起きない
    actions.Advance(NS::ActionClock{.clip = &clip});
    EXPECT_TRUE(actions.StartedEvents().empty());
    actions.Advance(NS::ActionClock{.clip = &clip, .clipTime = step * 2.5f});
    EXPECT_EQ(actions.StartedEvents(), (Indices{1, 2}));
    EXPECT_EQ(actions.Frame(), 2);
    actions.Advance(NS::ActionClock{.clip = &clip, .clipTime = step * 4.5f});
    EXPECT_EQ(actions.StartedEvents(), Indices{3});
}

// クリップの時刻を 1 フレームずつ足しても、float の誤差で事象が次のフレームへずれない
TEST(ActorAction, TheClipTimeAddsUpOneFrameAtATime)
{
    NS::ActorActionInfo action;
    action.name = "Wave";
    for (int frame = 0; frame < 60; ++frame)
    {
        action.events.push_back(SoundEvent("F", frame));
    }
    ActionScene probes({action});
    ASSERT_NE(probes.self, nullptr);
    NS::ActorAction& actions = *probes.self->ActorActionClassObj();
    const float step = NS::OS::FrameTimer::FixedDelta();
    const NS::Anim::AnimClip clip = ClipOf(step * 120.0f);
    ASSERT_TRUE(actions.Start("Wave"));
    actions.Advance(NS::ActionClock{});
    actions.BindClip(NS::ActionClock{.clip = &clip});
    float time = 0.0f;
    for (int frame = 1; frame < 60; ++frame)
    {
        time += step;
        actions.Advance(NS::ActionClock{.clip = &clip, .clipTime = time});
        ASSERT_EQ(actions.Frame(), frame);
        ASSERT_EQ(actions.StartedEvents(), Indices{static_cast<std::size_t>(frame)});
    }
}

// 結んだクリップが 1 周すると、フレーム 0 から事象をもう一度起こす
TEST(ActorAction, ALoopOfTheBoundClipStartsTheEventsAgain)
{
    NS::ActorActionInfo action;
    action.name = "Step";
    action.events = {SoundEvent("A", 0), SoundEvent("B", 1)};
    ActionScene probes({action});
    ASSERT_NE(probes.self, nullptr);
    NS::ActorAction& actions = *probes.self->ActorActionClassObj();
    const float step = NS::OS::FrameTimer::FixedDelta();
    const NS::Anim::AnimClip clip = ClipOf(step * 3.0f);
    ASSERT_TRUE(actions.Start("Step"));
    actions.Advance(NS::ActionClock{});
    actions.BindClip(NS::ActionClock{.clip = &clip});
    actions.Advance(NS::ActionClock{.clip = &clip, .clipTime = step * 1.5f});
    EXPECT_EQ(actions.StartedEvents(), Indices{1});

    actions.Advance(NS::ActionClock{.clip = &clip, .clipTime = step * 0.2f, .clipLoops = 1});
    EXPECT_EQ(actions.StartedEvents(), Indices{0});
    EXPECT_EQ(actions.Frame(), 0);
    actions.Advance(NS::ActionClock{.clip = &clip, .clipTime = step * 1.2f, .clipLoops = 1});
    EXPECT_EQ(actions.StartedEvents(), Indices{1});
}

// 結んだクリップが別のクリップへ替わると動作は止まる。クリップが無いまま結んだ動作と、長さが 0 のクリップに
// 結んだ動作はフレーム 0 のまま
TEST(ActorAction, AnotherClipStopsTheActionAndNoClipHoldsIt)
{
    NS::ActorActionInfo action;
    action.name = "Step";
    action.events = {SoundEvent("A", 0), SoundEvent("B", 1)};
    ActionScene probes({action});
    ASSERT_NE(probes.self, nullptr);
    NS::ActorAction& actions = *probes.self->ActorActionClassObj();
    const float step = NS::OS::FrameTimer::FixedDelta();
    const NS::Anim::AnimClip bound = ClipOf(step * 30.0f);
    const NS::Anim::AnimClip other = ClipOf(step * 30.0f);
    const NS::Anim::AnimClip empty = ClipOf(0.0f);

    ASSERT_TRUE(actions.Start("Step"));
    actions.Advance(NS::ActionClock{});
    actions.BindClip(NS::ActionClock{.clip = &bound});
    actions.Advance(NS::ActionClock{.clip = &other, .clipTime = step * 5.0f, .clipLoops = 2});
    EXPECT_TRUE(actions.StartedEvents().empty());
    EXPECT_FALSE(actions.IsRunning());

    for (const NS::Anim::AnimClip* clip : {static_cast<const NS::Anim::AnimClip*>(nullptr), &empty})
    {
        ASSERT_TRUE(actions.Start("Step"));
        actions.Advance(NS::ActionClock{});
        actions.BindClip(NS::ActionClock{.clip = clip});
        actions.Advance(NS::ActionClock{.clip = clip, .clipTime = step * 5.0f, .clipLoops = 1});
        EXPECT_TRUE(actions.StartedEvents().empty());
        EXPECT_EQ(actions.Frame(), 0);
        EXPECT_TRUE(actions.IsRunning());
    }
}

// 流れていたクリップの途中で結ぶと、結んだ時刻から数える。前のフレームの事象をまとめて起こさない
TEST(ActorAction, BindingMidClipCountsFromTheBoundTime)
{
    NS::ActorActionInfo action;
    action.name = "Step";
    for (int frame = 0; frame < 8; ++frame)
    {
        action.events.push_back(SoundEvent("F", frame));
    }
    ActionScene probes({action});
    ASSERT_NE(probes.self, nullptr);
    NS::ActorAction& actions = *probes.self->ActorActionClassObj();
    const float step = NS::OS::FrameTimer::FixedDelta();
    const NS::Anim::AnimClip clip = ClipOf(step * 30.0f);
    ASSERT_TRUE(actions.Start("Step"));
    actions.Advance(NS::ActionClock{});
    actions.BindClip(NS::ActionClock{.clip = &clip, .clipTime = step * 5.5f});
    actions.Advance(NS::ActionClock{.clip = &clip, .clipTime = step * 6.2f});
    EXPECT_EQ(actions.StartedEvents(), Indices{6});
}

// 1 周の間に Advance が来ずにクリップの終わりを越えても、前の周の残りのフレームを起こしてからフレーム 0 へ戻る
TEST(ActorAction, ALoopRunsTheRestOfThePreviousLapFirst)
{
    NS::ActorActionInfo action;
    action.name = "Step";
    action.events = {SoundEvent("A", 0), SoundEvent("B", 1), SoundEvent("C", 2)};
    ActionScene probes({action});
    ASSERT_NE(probes.self, nullptr);
    NS::ActorAction& actions = *probes.self->ActorActionClassObj();
    const float step = NS::OS::FrameTimer::FixedDelta();
    const NS::Anim::AnimClip clip = ClipOf(step * 3.0f);
    ASSERT_TRUE(actions.Start("Step"));
    actions.Advance(NS::ActionClock{});
    actions.BindClip(NS::ActionClock{.clip = &clip});
    actions.Advance(NS::ActionClock{.clip = &clip, .clipTime = step * 1.2f});
    EXPECT_EQ(actions.StartedEvents(), Indices{1});
    actions.Advance(NS::ActionClock{.clip = &clip, .clipTime = step * 0.5f, .clipLoops = 1});
    EXPECT_EQ(actions.StartedEvents(), (Indices{2, 0}));
}

// 1 回の Advance の間に 2 周以上したら、過ぎた周の事象も起こす
TEST(ActorAction, SkippedLapsRunTheirEventsToo)
{
    NS::ActorActionInfo action;
    action.name = "Step";
    action.events = {SoundEvent("A", 0), SoundEvent("B", 1)};
    ActionScene probes({action});
    ASSERT_NE(probes.self, nullptr);
    NS::ActorAction& actions = *probes.self->ActorActionClassObj();
    const float step = NS::OS::FrameTimer::FixedDelta();
    const NS::Anim::AnimClip clip = ClipOf(step * 2.0f);
    ASSERT_TRUE(actions.Start("Step"));
    actions.Advance(NS::ActionClock{});
    actions.BindClip(NS::ActionClock{.clip = &clip});
    actions.Advance(NS::ActionClock{.clip = &clip, .clipTime = step * 0.5f, .clipLoops = 3});
    // 0 周目の残り (1)、1 周目と 2 周目 (0・1 を 2 回)、3 周目の頭 (0)
    EXPECT_EQ(actions.StartedEvents(), (Indices{1, 0, 1, 0, 1, 0}));
}

// クリップを結ばなかった動作は、ほかのクリップが流れていてもフレーム 0 のまま残る
TEST(ActorAction, AnActionWithoutItsClipHoldsWhileAnotherClipPlays)
{
    NS::ActorActionInfo action;
    action.name = "Step";
    action.events = {SoundEvent("A", 0), SoundEvent("B", 1)};
    ActionScene probes({action});
    ASSERT_NE(probes.self, nullptr);
    NS::ActorAction& actions = *probes.self->ActorActionClassObj();
    const float step = NS::OS::FrameTimer::FixedDelta();
    const NS::Anim::AnimClip clip = ClipOf(step * 30.0f);
    ASSERT_TRUE(actions.Start("Step"));
    actions.Advance(NS::ActionClock{});
    actions.BindClip(NS::ActionClock{});
    actions.Advance(NS::ActionClock{.clip = &clip, .clipTime = step * 2.0f, .clipLoops = 1});
    EXPECT_TRUE(actions.StartedEvents().empty());
    EXPECT_TRUE(actions.IsRunning());
    EXPECT_EQ(actions.Frame(), 0);
}

// 同じクリップが頭から流れ直すと (ループの回数が減ると)、フレーム 0 から起こし直す
TEST(ActorAction, TheSameClipRestartedStartsTheEventsAgain)
{
    NS::ActorActionInfo action;
    action.name = "Step";
    action.events = {SoundEvent("A", 0), SoundEvent("B", 1), SoundEvent("C", 5)};
    ActionScene probes({action});
    ASSERT_NE(probes.self, nullptr);
    NS::ActorAction& actions = *probes.self->ActorActionClassObj();
    const float step = NS::OS::FrameTimer::FixedDelta();
    const NS::Anim::AnimClip clip = ClipOf(step * 10.0f);
    ASSERT_TRUE(actions.Start("Step"));
    actions.Advance(NS::ActionClock{});
    actions.BindClip(NS::ActionClock{.clip = &clip, .clipLoops = 2});
    actions.Advance(NS::ActionClock{.clip = &clip, .clipTime = step * 5.5f, .clipLoops = 2});
    EXPECT_EQ(actions.StartedEvents(), (Indices{1, 2}));
    actions.Advance(NS::ActionClock{.clip = &clip, .clipTime = step * 1.5f});
    EXPECT_EQ(actions.StartedEvents(), (Indices{0, 1}));
}

// アニメーションはクリップが 1 周するたびにループの回数を足し、別のクリップを流すと 0 に戻す
TEST(ActorAction, TheAnimationCountsTheLoopsOfItsClip)
{
    const float step = NS::OS::FrameTimer::FixedDelta();
    const NS::Anim::AnimClip first = ClipOf(step * 3.0f);
    const NS::Anim::AnimClip second = ClipOf(step * 3.0f);
    NS::Model model;
    NS::Animation animation;
    animation.Play(&first);
    for (int frame = 0; frame < 7; ++frame)
    {
        animation.Update(&model, step);
    }
    EXPECT_EQ(animation.LoopCount(), 2u);
    animation.Play(&first);
    EXPECT_EQ(animation.LoopCount(), 2u);
    animation.Play(&second);
    EXPECT_EQ(animation.LoopCount(), 0u);
}

// 名前で始めた動作は、始めた呼び出しの中でフレーム 0 の事象を起こす。
// 次の Update まで待つと、クリップの切り替えが 1 フレーム遅れる
TEST(ActorAction, StartingByNameRunsTheFirstFrameAtOnce)
{
    NS::ActorActionInfo action;
    action.name = "Wave";
    action.events = {SoundEvent("A", 0), SoundEvent("B", 1)};
    ActionScene probes({action});
    ASSERT_NE(probes.self, nullptr);
    ASSERT_TRUE(probes.self->StartAction("Wave"));
    const NS::ActorAction& actions = *probes.self->ActorActionClassObj();
    EXPECT_EQ(actions.Frame(), 0);
    EXPECT_EQ(actions.StartedEvents(), Indices{0});
}

// 始まった項目は持ち主の SceneActor が自分の場面へ頼む。画面の光の項目は、場面の画面の光を始める
TEST(ActorAction, TheOwnerRunsTheStartedItems)
{
    NS::ActionScreenInfo screen;
    screen.alpha = 0.6f;
    NS::ActorActionInfo action;
    action.name = "Run";
    action.events = {{screen, 1, 6}};
    ActionScene probes({action});
    ASSERT_NE(probes.self, nullptr);
    probes.Enter("Run");
    probes.Step();
    EXPECT_EQ(probes.scene.ScreenFlash().FramesRemaining(), 0);
    probes.Step();
    EXPECT_GT(probes.scene.ScreenFlash().FramesRemaining(), 0);
}

// 止めの間は状態の歩が進まないので、動作の時計も進まない
TEST(ActorAction, TheClockHoldsDuringTheHitStop)
{
    NS::ActorActionInfo action;
    action.name = "Run";
    action.events = {SoundEvent("A", 0), SoundEvent("B", 1)};
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
        EXPECT_EQ(probes.self->ActorActionClassObj()->Frame(), 0);
    }
    probes.Step();
    EXPECT_EQ(probes.Started(), (Names{"A", "B"}));
}

// 状態が移ると、前の動作の残りの事象は起きない
TEST(ActorAction, LeavingTheStateDropsTheRestOfTheAction)
{
    NS::ActorActionInfo action;
    action.name = "Run";
    action.events = {SoundEvent("A", 0), SoundEvent("B", 3)};
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
        EXPECT_FALSE(probes.self->ActorActionClassObj()->IsRunning());
    }
}

// センサーの入り切りは始まりの歩に効き、終わりの歩に戻る。途中で状態を出ても戻る
TEST(ActorAction, SensorSwitchesHoldForTheirLength)
{
    NS::ActorActionInfo flicker;
    flicker.name = "Flicker";
    flicker.sensors = {{.sensor = "Body", .valid = false, .start = 1, .length = 2}};
    NS::ActorActionInfo hold;
    hold.name = "Hold";
    hold.sensors = {{.sensor = "Attack", .valid = false, .start = 0, .length = 10}};
    ActionScene probes({flicker, hold});
    ASSERT_NE(probes.self, nullptr);
    const NS::HitSensor& body = *probes.self->HitSensorsClassObj()->Find("Body");
    const NS::HitSensor& attack = *probes.self->HitSensorsClassObj()->Find("Attack");

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

    // 状態の中で動作を止めても、入り切りの途中のセンサーはその場で戻る
    probes.Enter("Hold");
    probes.Step();
    ASSERT_FALSE(attack.IsValid());
    probes.self->StopAction();
    EXPECT_TRUE(attack.IsValid());
}

// 持ち主に無い名前の入り切りは、WARN を出してどのセンサーも切り替えない
TEST(ActorAction, SensorSwitchOfAMissingNameChangesNothing)
{
    NS::ActorActionInfo missing;
    missing.name = "Missing";
    missing.sensors = {{.sensor = "Tail", .valid = false, .start = 0, .length = 10}};
    ActionScene probes({missing});
    ASSERT_NE(probes.self, nullptr);
    const NS::HitSensor& body = *probes.self->HitSensorsClassObj()->Find("Body");
    const NS::HitSensor& attack = *probes.self->HitSensorsClassObj()->Find("Attack");

    const TestLogCapture log;
    probes.Enter("Missing");
    probes.Step();
    EXPECT_TRUE(body.IsValid());
    EXPECT_TRUE(attack.IsValid());
    EXPECT_EQ(log.WarningCount(), 1);
}

// 名前の無い動作は始めず、知らせる
TEST(ActorAction, AMissingActionIsNotStarted)
{
    ActionScene probes({});
    ASSERT_NE(probes.self, nullptr);
    const TestLogCapture log;
    probes.Enter("Nothing");
    probes.Step();
    EXPECT_FALSE(probes.self->started);
    EXPECT_FALSE(probes.self->ActorActionClassObj()->IsRunning());
    EXPECT_EQ(log.WarningCount(), 1);
}

// 状態の OnStep が動作の長さの回数だけ走ると、次の OnStep から状態の終わりと答える
TEST(ActorAction, TheStateEndsWhenItsStepsReachTheActionLength)
{
    NS::ActorActionInfo action;
    action.name = "Run";
    action.length = 3;
    ActionScene probes({action});
    ASSERT_NE(probes.self, nullptr);
    probes.Enter("Run");
    for (int frame = 0; frame < 5; ++frame)
    {
        probes.Step();
    }
    EXPECT_EQ(probes.self->endSeen, (std::vector<bool>{false, false, false, true, true}));
}

// 長さを決めていない動作・始まらなかった動作・動作の無いアクター・移る予約の後は、状態の終わりと答えない
TEST(ActorAction, TheStateDoesNotEndWithoutTheBoundActionLength)
{
    NS::SceneActor plain;
    EXPECT_FALSE(NS::IsStateEnd(&plain));

    NS::ActorActionInfo unset;
    unset.name = "Unset";
    NS::ActorActionInfo run;
    run.name = "Run";
    run.length = 1;
    ActionScene probes({unset, run});
    ASSERT_NE(probes.self, nullptr);
    probes.Enter("Unset");
    for (int frame = 0; frame < 3; ++frame)
    {
        probes.Step();
    }
    EXPECT_EQ(probes.self->endSeen, (std::vector<bool>{false, false, false}));

    (void)NS::SetState<ActionIdleState>(probes.self);
    probes.Step();
    probes.self->endSeen.clear();
    {
        const TestLogCapture log;
        probes.Enter("Nothing");
        for (int frame = 0; frame < 3; ++frame)
        {
            probes.Step();
        }
    }
    EXPECT_EQ(probes.self->endSeen, (std::vector<bool>{false, false, false}));

    (void)NS::SetState<ActionIdleState>(probes.self);
    probes.Step();
    probes.Enter("Run");
    probes.Step();
    probes.Step();
    ASSERT_TRUE(NS::IsStateEnd(probes.self));
    // 移る予約の間は、移る先の状態を前の状態に結んだ動作の長さで答えない
    (void)NS::SetState<ActionIdleState>(probes.self);
    EXPECT_FALSE(NS::IsStateEnd(probes.self));
}

// 途中で動作が止まっても (Stop・読み直し)、状態は動作を始めた時の長さで終わる
TEST(ActorAction, TheStateEndsEvenIfTheActionStopsMidway)
{
    NS::ActorActionInfo run;
    run.name = "Run";
    run.length = 3;
    ActionScene probes({run});
    ASSERT_NE(probes.self, nullptr);
    probes.Enter("Run");
    probes.Step();
    probes.self->StopAction();
    for (int frame = 0; frame < 3; ++frame)
    {
        probes.Step();
    }
    EXPECT_EQ(probes.self->endSeen, (std::vector<bool>{false, false, false, true}));

    (void)NS::SetState<ActionIdleState>(probes.self);
    probes.Step();
    probes.self->endSeen.clear();
    probes.Enter("Run");
    probes.Step();
    probes.self->ReloadActions();
    for (int frame = 0; frame < 3; ++frame)
    {
        probes.Step();
    }
    EXPECT_EQ(probes.self->endSeen, (std::vector<bool>{false, false, false, true}));
}

// 動作を始めずに同じ状態へ入り直すと、前に入った時の動作の長さでは終わらない
TEST(ActorAction, ReenteringWithoutStartDoesNotUseThePreviousLength)
{
    NS::ActorActionInfo run;
    run.name = "Run";
    run.length = 2;
    ActionScene probes({run});
    ASSERT_NE(probes.self, nullptr);
    probes.Enter("Run");
    for (int frame = 0; frame < 3; ++frame)
    {
        probes.Step();
    }
    ASSERT_EQ(probes.self->endSeen, (std::vector<bool>{false, false, true}));

    probes.self->StopAction();
    (void)NS::SetState<ActionIdleState>(probes.self);
    probes.Step();
    probes.self->endSeen.clear();
    probes.self->startOnEnter = false;
    probes.Enter("Run");
    for (int frame = 0; frame < 4; ++frame)
    {
        probes.Step();
    }
    EXPECT_EQ(probes.self->endSeen, (std::vector<bool>{false, false, false, false}));
}

// 予約し直した先が動作を始めた状態と同じでも、移る予約の間は状態の終わりと答えない
TEST(ActorAction, TheStateDoesNotEndWhileReservedBackIntoTheSameState)
{
    NS::ActorActionInfo run;
    run.name = "Run";
    run.length = 1;
    ActionScene probes({run});
    ASSERT_NE(probes.self, nullptr);
    probes.Enter("Run");
    probes.Step();
    ASSERT_TRUE(probes.self->ActorActionClassObj()->IsRunning());
    (void)NS::SetState<ActionIdleState>(probes.self);
    (void)NS::SetState<ActionRunState>(probes.self);
    EXPECT_FALSE(NS::IsStateEnd(probes.self));
}
