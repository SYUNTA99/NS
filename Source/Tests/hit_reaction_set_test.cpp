#include "NSlib/CoreObject/ClassObjs/ActionTimeline.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/CoreObject/System/HitScreenDirector.h"
#include "NSlib/Windows/FileSystem.h"
#include "NSlib/Windows/Input.h"
#include "TestActorFile.h"
#include "TestLogCapture.h"
#include "TestViewCamera.h"

#include <gtest/gtest.h>

#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// 名前の束の項目の型・束のファイルの形・読み込みと保存の往復・壊れた束の扱い・束を始める口と止めを縛る

namespace
{
    // 項目の種類ごとに 1 つずつ、欄を既定から動かした束
    NS::HitReactionSet MakeEveryKindSet()
    {
        NS::HitReactionSet set;
        set.name = "TackleCenter";
        set.stopFrames = 12;

        NS::HitShapeItem shape;
        shape.along.count = 2;
        shape.along.keys[0] = NS::Curve::Key{0.0f, 0.7f};
        shape.along.keys[1] = NS::Curve::Key{11.0f, 0.7f};
        shape.height.count = 1;
        shape.height.keys[0] = NS::Curve::Key{0.0f, 1.1f};
        NS::HitBodyShakeItem shake;
        shake.amplitudePixels = 45.0f;
        shake.flipFrames = 2;
        NS::HitBodyTremorItem tremor;
        tremor.amplitudePixels = 1.5f;
        tremor.radiusRatio = 0.2f;
        NS::HitCameraSinkItem sink;
        sink.bottomPixels = 7.5f;
        sink.tremblePixels = 4.0f;
        NS::HitZoomRollItem zoom;
        zoom.zoom = 1.1f;
        zoom.delayFrames = 6;
        NS::HitCameraTraumaItem trauma;
        trauma.trauma = 1.0f;
        NS::HitCameraLurchItem lurch;
        lurch.distance.count = 2;
        lurch.distance.keys[0] = NS::Curve::Key{0.0f, 0.3f};
        lurch.distance.keys[1] = NS::Curve::Key{8.0f, 0.0f};
        NS::HitCameraReboundSwayItem sway;
        sway.distance.count = 1;
        sway.distance.keys[0] = NS::Curve::Key{0.0f, -0.5f};
        NS::HitFlashItem flash;
        flash.alpha = 0.9f;
        NS::HitDistortionRingItem ring;
        ring.halfWidth = 0.06f;
        NS::HitGroundWaveItem wave;
        wave.radius.count = 2;
        wave.radius.keys[0] = NS::Curve::Key{0.0f, 0.5f};
        wave.radius.keys[1] = NS::Curve::Key{10.0f, 6.0f};
        NS::HitPadVibrationItem pad;
        pad.byDirection = true;
        pad.upLeft.count = 1;
        pad.upLeft.keys[0] = NS::Curve::Key{0.0f, 0.8f};
        pad.downRight.count = 1;
        pad.downRight.keys[0] = NS::Curve::Key{2.0f, 0.3f};
        NS::HitWorldSpeedItem speed;
        speed.startSpeed = 0.2f;
        speed.returnSeconds = 0.3f;
        NS::HitOwnerItem ghost;
        ghost.name = "Ghost";
        ghost.stopsDuringHitStop = true;
        ghost.values = {{"離れの画素", 112.5f}, {"明けの残像のフレーム数", 0.0f}};

        set.entries = {
            {shape, 18},
            {shake, 12},
            {tremor, 4},
            {sink, 33},
            {NS::HitCameraSinkReleaseItem{}, 1},
            {zoom, 7},
            {trauma, 1},
            {lurch, 9},
            {sway, 29},
            {flash, 2},
            {ring, 14},
            {wave, 20},
            {pad, 16},
            {speed, 1},
            {NS::HitOthersStopItem{}, 2},
            {ghost, 12},
        };
        return set;
    }
} // namespace

TEST(HitReactionSet, ValueTypeFieldsRoundTripThroughJson)
{
    NS::HitCameraSinkItem sink;
    sink.bottomPixels = 7.5f;
    sink.bouncePeriodFrames = 10;
    const NS::ReflectionInfo& info = *NS::HitCameraSinkItem::StaticReflection();
    const nlohmann::json fields = NS::SerializeReflectedFields(&sink, info);
    EXPECT_FLOAT_EQ(fields["底の深さのピクセル"].get<float>(), 7.5f);
    EXPECT_EQ(fields["跳ね返りの 1 往復"].get<int>(), 10);
    // 威力の掛けは無くなり、深さは束に書いた値のまま
    EXPECT_FALSE(fields.contains("威力の基準"));

    NS::HitCameraSinkItem read;
    EXPECT_EQ(NS::ApplyReflectedFields(&read, info, fields), 0u);
    EXPECT_FLOAT_EQ(read.bottomPixels, 7.5f);
    EXPECT_EQ(read.bouncePeriodFrames, 10);
}

TEST(HitReactionSet, EveryKindRoundTripsThroughTheFileForm)
{
    const NS::HitReactionSet written = MakeEveryKindSet();
    ASSERT_EQ(written.entries.size(), std::variant_size_v<NS::HitReactionItem>);
    const nlohmann::json doc = NS::HitReactionSetToJson(written);
    EXPECT_EQ(doc["name"].get<std::string>(), "TackleCenter");
    EXPECT_EQ(doc["stopFrames"].get<int>(), 12);
    ASSERT_EQ(doc["items"].size(), written.entries.size());
    EXPECT_EQ(doc["items"][0]["type"].get<std::string>(), "Shape");
    EXPECT_EQ(doc["items"][15]["fields"]["値"]["離れの画素"].get<float>(), 112.5f);

    std::string error;
    const std::optional<NS::HitReactionSet> read = NS::ParseHitReactionSet(doc, error);
    ASSERT_TRUE(read.has_value()) << error;
    EXPECT_EQ(read->name, written.name);
    EXPECT_EQ(read->stopFrames, written.stopFrames);
    ASSERT_EQ(read->entries.size(), written.entries.size());
    for (std::size_t i = 0; i < written.entries.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(read->entries[i].item.index(), written.entries[i].item.index());
        EXPECT_EQ(read->entries[i].length, written.entries[i].length);
    }
    EXPECT_EQ(NS::HitReactionSetToJson(*read), doc);

    const NS::HitPadVibrationItem* pad = std::get_if<NS::HitPadVibrationItem>(&read->entries[12].item);
    ASSERT_NE(pad, nullptr);
    EXPECT_TRUE(pad->byDirection);
    EXPECT_EQ(pad->upLeft, std::get<NS::HitPadVibrationItem>(written.entries[12].item).upLeft);
    const NS::HitZoomRollItem* zoom = std::get_if<NS::HitZoomRollItem>(&read->entries[5].item);
    ASSERT_NE(zoom, nullptr);
    EXPECT_EQ(zoom->delayFrames, 6);
    const NS::HitOwnerItem* ghost = std::get_if<NS::HitOwnerItem>(&read->entries[15].item);
    ASSERT_NE(ghost, nullptr);
    EXPECT_EQ(ghost->name, "Ghost");
    EXPECT_TRUE(ghost->stopsDuringHitStop);
    EXPECT_EQ(ghost->values, std::get<NS::HitOwnerItem>(written.entries[15].item).values);
}

TEST(HitReactionSet, MissingFieldsKeepTheDefaultsAndUnknownFieldsAreSkipped)
{
    const nlohmann::json doc = nlohmann::json::parse(R"({"name": "TackleMiss", "stopFrames": 4, "古い欄": 1, "items": [
        {"type": "Flash", "length": 2, "fields": {"濃さ": 0.3, "消した欄": 2}},
        {"type": "ZoomRoll", "length": 7},
        {"type": "Owner", "length": 1, "fields": {"名前": "HitEffect"}}
    ]})");
    std::string error;
    const TestLogCapture log;
    const std::optional<NS::HitReactionSet> read = NS::ParseHitReactionSet(doc, error);
    ASSERT_TRUE(read.has_value()) << error;
    ASSERT_EQ(read->entries.size(), 3u);
    EXPECT_FLOAT_EQ(std::get<NS::HitFlashItem>(read->entries[0].item).alpha, 0.3f);
    EXPECT_EQ(std::get<NS::HitZoomRollItem>(read->entries[1].item).delayFrames, NS::HitZoomRollItem{}.delayFrames);
    const NS::HitOwnerItem& owner = std::get<NS::HitOwnerItem>(read->entries[2].item);
    EXPECT_EQ(owner.name, "HitEffect");
    EXPECT_EQ(owner.stopsDuringHitStop, NS::HitOwnerItem{}.stopsDuringHitStop);
    EXPECT_TRUE(owner.values.empty());
    // 知らない欄は束の頭と項目の欄の 2 か所で知らせて飛ばす
    EXPECT_EQ(log.WarningCount(), 2);
    EXPECT_EQ(log.ErrorCount(), 0);
}

TEST(HitReactionSet, BrokenSetsAreRejectedWithAReason)
{
    const char* const broken[] = {
        R"({"stopFrames": 1, "items": []})",
        R"({"name": "", "stopFrames": 1, "items": []})",
        R"({"name": "A", "stopFrames": -1, "items": []})",
        R"({"name": "A", "stopFrames": 1.5, "items": []})",
        R"({"name": "A", "stopFrames": 1})",
        R"({"name": "A", "stopFrames": 1, "items": [{"type": "Explode", "length": 1}]})",
        R"({"name": "A", "stopFrames": 1, "items": [{"type": "Flash", "length": -1}]})",
        R"({"name": "A", "stopFrames": 1, "items": [{"type": "Flash"}]})",
        R"({"name": "A", "stopFrames": 1, "items": [{"type": "Flash", "length": 1, "fields": 3}]})",
        R"({"name": "A", "stopFrames": 1, "items": [{"type": "Owner", "length": 1, "fields": {"値": {"a": "x"}}}]})",
        // 威力で変わる値を消した種類は、名前を変えずに残すと値が黙って既定に戻る
        R"({"name": "A", "stopFrames": 1, "items": [{"type": "CameraShake", "length": 1}]})",
        R"([1, 2])",
    };
    for (const char* text : broken)
    {
        SCOPED_TRACE(text);
        std::string error;
        EXPECT_FALSE(NS::ParseHitReactionSet(nlohmann::json::parse(text), error).has_value());
        EXPECT_FALSE(error.empty());
    }
}

// .actor の束の並びは、壊れた束と名前の重なった束を ERROR で知らせて読まず、残りを読む
TEST(HitReactionSet, ActorFileSkipsBrokenAndDuplicateSets)
{
    nlohmann::json actorFile = ClassOnlyActorFile("PropActor");
    NS::SetActorFileHitReactions(actorFile, nlohmann::json::parse(R"([
        {"name": "Hit", "stopFrames": 12, "items": []},
        {"name": "Broken", "stopFrames": -1, "items": []},
        {"name": "Hit", "stopFrames": 4, "items": []},
        {"name": "Release", "stopFrames": 0, "items": []}
    ])"));
    const TestLogCapture log;
    const std::vector<NS::HitReactionSet> sets = NS::ReadHitReactionSets("PropActor", actorFile);
    ASSERT_EQ(sets.size(), 2u);
    EXPECT_EQ(sets[0].name, "Hit");
    EXPECT_EQ(sets[0].stopFrames, 12);
    EXPECT_EQ(sets[1].name, "Release");
    EXPECT_EQ(log.ErrorCount(), 2);
}

// 束と動作を書いた .actor を読んで書くと、バイトまで同じ。型へ読んで書き戻しても同じ
TEST(HitReactionSet, ActorFileWithSetsAndActionsRoundTripsByteForByte)
{
    using NS::OS::FileSystem;
    // 読み込みは部品の欄 classObjs を必ず持たせるので、初めから持つ形で書く
    nlohmann::json actorFile = ClassOnlyActorFile("PropActor");
    (void)NS::ObjectJsonClassObjs(actorFile);
    NS::WriteHitReactionSets(actorFile, {MakeEveryKindSet()});
    NS::ActorAction action;
    action.name = "BeforeContact";
    action.events = {{NS::HitOthersStopItem{}, 4, 2}, {NS::HitFlashItem{}, 0, 6}};
    NS::WriteActorActions(actorFile, {action});
    const std::string text = actorFile.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);

    const ScopedActorFileDirectory scope("SetsAndActions");
    WriteActorFile(scope.Path(), "Rock", text);
    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    library.Reload();
    const nlohmann::json* loaded = library.Find("Rock");
    ASSERT_NE(loaded, nullptr);
    ASSERT_TRUE(library.Save("Rock"));
    const std::optional<std::string> written = FileSystem::ReadAllText(FileSystem::Combine(scope.Path(), "Rock.actor"));
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(*written, text);

    nlohmann::json rewritten = *loaded;
    NS::SetActorFileHitReactions(rewritten, nlohmann::json::array());
    NS::SetActorFileActions(rewritten, nlohmann::json::array());
    ASSERT_NE(rewritten.dump(2, ' ', false, nlohmann::json::error_handler_t::replace), text);
    NS::WriteHitReactionSets(rewritten, NS::ReadHitReactionSets("Rock", *loaded));
    NS::WriteActorActions(rewritten, NS::ReadActorActions("Rock", *loaded));
    EXPECT_EQ(rewritten.dump(2, ' ', false, nlohmann::json::error_handler_t::replace), text);
}

// 出荷の .actor を全部、束と動作の型へ読んで書き戻すと、文書が同じ。束と動作の無い .actor には鍵を足さない
TEST(HitReactionSet, ShippedActorFilesRoundTripThroughTheTypes)
{
    using NS::OS::FileSystem;
    const std::string directory =
        FileSystem::Combine(FileSystem::Combine(FileSystem::ContentRoot(), "Assets"), "Actors");
    const std::vector<std::string> paths = FileSystem::ListFiles(directory, ".actor");
    ASSERT_FALSE(paths.empty());
    const TestLogCapture log;
    for (const std::string& path : paths)
    {
        SCOPED_TRACE(path);
        const std::optional<std::string> text = FileSystem::ReadAllText(path);
        ASSERT_TRUE(text.has_value());
        const nlohmann::json original = nlohmann::json::parse(*text);
        nlohmann::json rewritten = original;
        const std::string name = FileSystem::Stem(path);
        NS::WriteHitReactionSets(rewritten, NS::ReadHitReactionSets(name, original));
        NS::WriteActorActions(rewritten, NS::ReadActorActions(name, original));
        EXPECT_EQ(rewritten, original);
    }
    EXPECT_EQ(log.ErrorCount(), 0);
}

// 束を始める口と止めの歩。束は .actor から読み、名前・位置・2 つのセンサーで始める

namespace
{
    // 状態と体の段を数え、模型・体のセンサー・当たりの返りを持つアクター
    class ReactionProbeActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_NONE(ReactionProbeActor, NS::SceneActor)

        int stateSteps = 0;
        int bodySteps = 0;

    protected:
        void OnInit() override
        {
            CreateClassObj(m_model, this);
            NS::ShapeHitSensor* sensor = CreateClassObj<NS::HitSensor, NS::ShapeHitSensor>(m_bodySensor, this);
            sensor->SetSphere(0.5f);
            CreateClassObj(m_hitReaction, this);
        }
        void StateStep() override { ++stateSteps; }
        void BodyStep() override { ++bodySteps; }
    };
} // namespace

NS_PLACEABLE(ReactionProbeActor, "試しの当たりの返り")

namespace
{
    constexpr const char* k_ProbeName = "ReactionProbeActor";

    NS::Curve ConstantCurve(float value)
    {
        NS::Curve curve;
        curve.count = 1;
        curve.keys[0] = NS::Curve::Key{0.0f, value};
        return curve;
    }

    NS::HitReactionSet SetOf(std::string name, int stopFrames, std::vector<NS::HitReactionEntry> entries)
    {
        NS::HitReactionSet set;
        set.name = std::move(name);
        set.stopFrames = stopFrames;
        set.entries = std::move(entries);
        return set;
    }

    // sets を書いた .actor を敷いてアクターを作る。束は作る時に読むので、敷くのは作る間だけ
    ReactionProbeActor* SpawnProbe(NS::Scene& scene,
                                   const std::vector<NS::HitReactionSet>& sets,
                                   const NS::Vector3& position)
    {
        nlohmann::json actorFile = ClassOnlyActorFile(k_ProbeName);
        NS::WriteHitReactionSets(actorFile, sets);
        const ScopedActorFile scoped(k_ProbeName, std::move(actorFile));
        ReactionProbeActor* probe = scene.SpawnObject<ReactionProbeActor>(k_ProbeName);
        if (probe != nullptr)
        {
            probe->Root().SetPosition(position);
        }
        return probe;
    }

    // 後ろから +z を見るカメラと、原点のアクター self と前の other を置いた場面
    struct ProbeScene
    {
        NS::Scene scene;
        ReactionProbeActor* self = nullptr;
        ReactionProbeActor* other = nullptr;

        ProbeScene(const std::vector<NS::HitReactionSet>& sets, const NS::Vector3& otherPosition)
        {
            (void)PlaceViewCamera(scene, NS::Vector3{0.0f, 3.0f, -6.0f}, NS::Vector3{0.0f, 0.5f, 4.0f});
            self = SpawnProbe(scene, sets, NS::Vector3{0.0f, 1.0f, 0.0f});
            other = SpawnProbe(scene, sets, otherPosition);
        }

        bool Start(ReactionProbeActor& owner, std::string_view name, const NS::Vector3& position)
        {
            ReactionProbeActor* partner = self;
            if (&owner == self)
            {
                partner = other;
            }
            return owner.HitReactionClassObj()->Start(
                name, position, owner.BodySensorClassObj(), partner->BodySensorClassObj());
        }

        // 1 固定フレーム。アクター・カメラ・画面とパッドの係の順に回す
        void Step()
        {
            scene.ExecutePhase(NS::UpdatePhase::Triggers);
            scene.ExecutePhase(NS::UpdatePhase::Camera);
            scene.ExecutePhase(NS::UpdatePhase::RenderPrep);
        }

        [[nodiscard]] NS::CameraManager& Cameras() { return *scene.GetCameraManager(); }

        [[nodiscard]] float SinkPixels()
        {
            const NS::CameraSinkModifier* sink = Cameras().FindModifier<NS::CameraSinkModifier>();
            if (sink == nullptr)
            {
                return 0.0f;
            }
            return sink->Pixels();
        }
    };

    float PadLeft()
    {
        return NS::OS::Input::Get().Gamepad().Vibration().left;
    }

    const NS::HitOwnerRun* FindOwnerRun(const NS::HitReaction& reaction, std::string_view name)
    {
        for (const NS::HitOwnerRun& run : reaction.OwnerRuns())
        {
            if (run.item.name == name)
            {
                return &run;
            }
        }
        return nullptr;
    }
} // namespace

// 名前の束を始めると、白・振動・寄り・ずれ・沈み・トラウマ・持ち主へ渡す項目が、始めたフレームに全部出る
// 値は束に書いた値のまま使う
TEST(HitReactionStart, EveryItemStartsOnTheSameFrame)
{
    NS::HitFlashItem flash;
    flash.alpha = 0.6f;
    NS::HitPadVibrationItem pad;
    pad.left = ConstantCurve(0.5f);
    NS::HitZoomRollItem zoom;
    zoom.zoom = 1.2f;
    zoom.returnFrames = 1;
    NS::HitCameraLurchItem lurch;
    lurch.distance = ConstantCurve(0.3f);
    NS::HitCameraSinkItem sink;
    sink.bottomPixels = 9.0f;
    sink.sinkFrames = 1;
    NS::HitCameraTraumaItem trauma;
    trauma.trauma = 0.3f;
    NS::HitOwnerItem spark;
    spark.name = "Spark";
    spark.values = {{"大きさ", 2.0f}};
    ProbeScene probes({SetOf("Hit",
                             0,
                             {{flash, 3}, {pad, 4}, {zoom, 4}, {lurch, 5}, {sink, 20}, {trauma, 1}, {spark, 2}})},
                      NS::Vector3{0.0f, 1.0f, 4.0f});
    ASSERT_NE(probes.self, nullptr);
    const NS::Vector3 contact{0.0f, 1.0f, 3.5f};
    ASSERT_TRUE(probes.Start(*probes.self, "Hit", contact));

    const NS::HitScreenDirector* screen = NS::FindSceneSystem<NS::HitScreenDirector>(probes.self);
    ASSERT_NE(screen, nullptr);
    EXPECT_EQ(screen->FlashFramesRemaining(), 3);
    EXPECT_FLOAT_EQ(screen->FlashAlpha(), 0.6f);
    EXPECT_FLOAT_EQ(PadLeft(), 0.5f);
    EXPECT_FLOAT_EQ(probes.Cameras().ZoomRoll().zoom, 1.2f);
    EXPECT_NE(probes.Cameras().FindModifier<NS::CameraNudgeModifier>(NS::CameraNudgeModifier::KindFor(false)),
              nullptr);
    EXPECT_FLOAT_EQ(probes.SinkPixels(), -9.0f);
    EXPECT_FLOAT_EQ(probes.Cameras().Trauma(), 0.3f);
    const NS::HitOwnerRun* run = FindOwnerRun(*probes.self->HitReactionClassObj(), "Spark");
    ASSERT_NE(run, nullptr);
    EXPECT_EQ(run->frame, 0);
    EXPECT_EQ(run->length, 2);
    EXPECT_EQ(run->position, contact);
    EXPECT_FLOAT_EQ(run->item.values.at("大きさ"), 2.0f);

    // 名前の無い束は始めず、知らせる
    const TestLogCapture log;
    EXPECT_FALSE(probes.Start(*probes.self, "Nothing", contact));
    EXPECT_EQ(log.WarningCount(), 1);
}

// 束の止めの長さの間は、持ち主の状態と体の段が進まない。白と描く揺れは係と模型の歩で進む
TEST(HitReactionStart, TheStopHoldsStateAndBodyButNotTheDirectors)
{
    NS::HitFlashItem flash;
    flash.alpha = 1.0f;
    NS::HitBodyShakeItem shake;
    shake.amplitudePixels = 20.0f;
    ProbeScene probes({SetOf("Hit", 3, {{flash, 6}, {shake, 6}})}, NS::Vector3{0.0f, 1.0f, 4.0f});
    ASSERT_NE(probes.self, nullptr);
    const NS::HitScreenDirector* screen = NS::FindSceneSystem<NS::HitScreenDirector>(probes.self);
    ASSERT_NE(screen, nullptr);
    ASSERT_TRUE(probes.Start(*probes.self, "Hit", NS::Vector3{0.0f, 1.0f, 3.5f}));
    EXPECT_TRUE(probes.self->IsHitStopping());
    for (int frame = 1; frame <= 3; ++frame)
    {
        SCOPED_TRACE(frame);
        probes.Step();
        EXPECT_EQ(probes.self->stateSteps, 0);
        EXPECT_EQ(probes.self->bodySteps, 0);
        EXPECT_NE(probes.self->ModelClassObj()->GetDrawOffset().x, 0.0f);
    }
    EXPECT_LT(screen->FlashFramesRemaining(), 6);
    EXPECT_FALSE(probes.self->IsHitStopping());
    // 相手は止まらない
    EXPECT_EQ(probes.other->stateSteps, 3);
    probes.Step();
    EXPECT_EQ(probes.self->stateSteps, 1);
    EXPECT_EQ(probes.self->bodySteps, 1);
}

// 持ち主へ渡す項目は、止めの間も止めるかの欄が立つ物だけが止めの間に進まない
TEST(HitReactionStart, OwnerItemsHoldDuringTheStopOnlyWhenAsked)
{
    NS::HitOwnerItem held;
    held.name = "Held";
    held.stopsDuringHitStop = true;
    NS::HitOwnerItem running;
    running.name = "Running";
    ProbeScene probes({SetOf("Hit", 3, {{held, 10}, {running, 10}})}, NS::Vector3{0.0f, 1.0f, 4.0f});
    ASSERT_NE(probes.self, nullptr);
    const NS::HitReaction& reaction = *probes.self->HitReactionClassObj();
    ASSERT_TRUE(probes.Start(*probes.self, "Hit", NS::Vector3{}));
    for (int frame = 0; frame < 3; ++frame)
    {
        probes.Step();
    }
    ASSERT_NE(FindOwnerRun(reaction, "Held"), nullptr);
    ASSERT_NE(FindOwnerRun(reaction, "Running"), nullptr);
    EXPECT_EQ(FindOwnerRun(reaction, "Held")->frame, 0);
    EXPECT_EQ(FindOwnerRun(reaction, "Running")->frame, 3);
    probes.Step();
    EXPECT_EQ(FindOwnerRun(reaction, "Held")->frame, 1);
    EXPECT_EQ(FindOwnerRun(reaction, "Running")->frame, 4);
    for (int frame = 0; frame < 6; ++frame)
    {
        probes.Step();
    }
    // 長さを過ぎた項目は外れる
    EXPECT_EQ(FindOwnerRun(reaction, "Running"), nullptr);
    EXPECT_NE(FindOwnerRun(reaction, "Held"), nullptr);
}

// 寄りは始めるまでのフレーム数を数えてから寄せる。束を始めたフレームのカメラの段は、始めたフレームの姿を描く
TEST(HitReactionStart, TheZoomWaitsItsDelayFrames)
{
    NS::HitZoomRollItem zoom;
    zoom.zoom = 1.2f;
    zoom.returnFrames = 1;
    zoom.delayFrames = 2;
    ProbeScene probes({SetOf("Hit", 0, {{zoom, 3}})}, NS::Vector3{0.0f, 1.0f, 4.0f});
    ASSERT_NE(probes.self, nullptr);
    ASSERT_TRUE(probes.Start(*probes.self, "Hit", NS::Vector3{}));
    EXPECT_FLOAT_EQ(probes.Cameras().ZoomRoll().zoom, 1.0f);
    probes.Step();
    EXPECT_FLOAT_EQ(probes.Cameras().ZoomRoll().zoom, 1.0f);
    probes.Step();
    EXPECT_FLOAT_EQ(probes.Cameras().ZoomRoll().zoom, 1.0f);
    probes.Step();
    EXPECT_FLOAT_EQ(probes.Cameras().ZoomRoll().zoom, 1.2f);
    probes.Step();
    EXPECT_FLOAT_EQ(probes.Cameras().ZoomRoll().zoom, 1.2f);
}

// つんのめり・反動の揺れ・寄りの傾き・横揺れの最初の振れは、自機のセンサーから相手のセンサーへの向きで決まる
// 震えと歪みの輪の中心は渡した位置
TEST(HitReactionStart, DirectionsComeFromTheTwoSensors)
{
    NS::HitCameraLurchItem lurch;
    lurch.distance = ConstantCurve(0.3f);
    NS::HitCameraReboundSwayItem sway;
    sway.distance = ConstantCurve(0.3f);
    NS::HitZoomRollItem zoom;
    zoom.zoom = 1.1f;
    zoom.rollDegrees = 3.0f;
    zoom.returnFrames = 1;
    NS::HitBodyShakeItem shake;
    shake.amplitudePixels = 20.0f;
    NS::HitBodyTremorItem tremor;
    NS::HitDistortionRingItem ring;
    ring.radius = ConstantCurve(0.2f);
    ring.push = ConstantCurve(0.1f);
    const std::vector<NS::HitReactionSet> sets = {
        SetOf("Hit", 0, {{lurch, 5}, {sway, 5}, {zoom, 4}, {shake, 6}, {tremor, 12}, {ring, 5}})};
    for (const float side : {2.0f, -2.0f})
    {
        SCOPED_TRACE(side);
        ProbeScene probes(sets, NS::Vector3{side, 1.0f, 4.0f});
        ASSERT_NE(probes.self, nullptr);
        const NS::Vector3 contact{side * 0.2f, 1.2f, 3.5f};
        ASSERT_TRUE(probes.Start(*probes.self, "Hit", contact));
        NS::Vector3 toward{side, 0.0f, 4.0f};
        toward.Normalize();
        const NS::CameraNudgeModifier* world =
            probes.Cameras().FindModifier<NS::CameraNudgeModifier>(NS::CameraNudgeModifier::KindFor(false));
        const NS::CameraNudgeModifier* screen =
            probes.Cameras().FindModifier<NS::CameraNudgeModifier>(NS::CameraNudgeModifier::KindFor(true));
        ASSERT_NE(world, nullptr);
        ASSERT_NE(screen, nullptr);
        EXPECT_NEAR((world->Desc().direction - toward).Length(), 0.0f, 1.0e-5f);
        EXPECT_NEAR((screen->Desc().direction + toward).Length(), 0.0f, 1.0e-5f);
        // 相手がカメラの右なら上端を右へ倒し、体は相手と逆の左から揺れる
        EXPECT_FLOAT_EQ(probes.Cameras().ZoomRoll().rollDegrees, 3.0f * std::copysign(1.0f, side));
        EXPECT_EQ(probes.scene.DistortionRingShown().center, contact);
        probes.Step();
        EXPECT_LT(probes.self->ModelClassObj()->GetDrawOffset().x * side, 0.0f);
        const NS::Vector3 offset = probes.self->ModelClassObj()->GetDrawTremor().contactOffset;
        EXPECT_NEAR((offset - (contact - probes.self->Root().Position())).Length(), 0.0f, 1.0e-5f);
    }
}

// 向き別の振動は、自機から見た相手の向きの角度で隣り合う 2 本を混ぜる。右上 45 度なら右と上を半分ずつ
TEST(HitReactionStart, DirectedVibrationBlendsTheTwoNeighborsTowardTheOther)
{
    NS::HitPadVibrationItem pad;
    pad.byDirection = true;
    pad.rightLeft = ConstantCurve(1.0f);
    pad.leftLeft = ConstantCurve(0.6f);
    pad.upLeft = ConstantCurve(0.3f);
    pad.downLeft = ConstantCurve(0.1f);
    ProbeScene probes({SetOf("Hit", 0, {{pad, 8}})}, NS::Vector3{1.0f, 2.0f, 4.0f});
    ASSERT_NE(probes.self, nullptr);
    ASSERT_TRUE(probes.Start(*probes.self, "Hit", NS::Vector3{}));
    EXPECT_NEAR(PadLeft(), 0.5f * 1.0f + 0.5f * 0.3f, 1.0e-4f);
    probes.Step();
    probes.Step();
    EXPECT_NEAR(PadLeft(), 0.5f * 1.0f + 0.5f * 0.3f, 1.0e-4f);
}

// 向きの重みは、角度で隣り合う 2 つの向きを直線に混ぜる。足すと 1 で、境目で急に変わらない
TEST(HitReactionStart, DirectionWeightsBlendTheTwoNeighborsByAngle)
{
    const auto weightsOf = [](float side, float up) {
        const NS::HitDirectionWeights weights = NS::BlendHitDirectionWeights(side, up);
        return std::vector<float>{weights.right, weights.left, weights.up, weights.down};
    };
    EXPECT_EQ(weightsOf(1.0f, 0.0f), (std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f}));
    EXPECT_EQ(weightsOf(0.0f, -0.4f), (std::vector<float>{0.0f, 0.0f, 0.0f, 1.0f}));
    EXPECT_NEAR(weightsOf(0.5f, 0.5f)[0], 0.5f, 1.0e-4f);
    EXPECT_NEAR(weightsOf(0.5f, 0.5f)[2], 0.5f, 1.0e-4f);
    EXPECT_NEAR(weightsOf(-0.3f, -0.3f)[1], 0.5f, 1.0e-4f);
    EXPECT_NEAR(weightsOf(-0.3f, -0.3f)[3], 0.5f, 1.0e-4f);
    // 真ん中は角度が決まらないので右へ全部を渡す
    EXPECT_EQ(weightsOf(0.0f, 0.0f), (std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f}));
    std::vector<float> previous;
    for (int step = 0; step <= 720; ++step)
    {
        const float angle = static_cast<float>(step) * 0.5f * NS::k_Pi / 180.0f;
        const std::vector<float> weights = weightsOf(0.7f * std::cos(angle), 0.7f * std::sin(angle));
        float sum = 0.0f;
        for (std::size_t i = 0; i < weights.size(); ++i)
        {
            EXPECT_GE(weights[i], 0.0f);
            sum += weights[i];
            if (!previous.empty())
            {
                EXPECT_LT(std::abs(weights[i] - previous[i]), 0.02f) << "角度 " << step * 0.5f;
            }
        }
        EXPECT_NEAR(sum, 1.0f, 1.0e-4f);
        previous = weights;
    }
}

// 束を始めた後に止めの残りを上書きすると、止めの長さだけが替わり、揺れの長さは束のまま
TEST(HitReactionStart, OverridingTheStopChangesOnlyTheStop)
{
    NS::HitBodyShakeItem shake;
    shake.amplitudePixels = 20.0f;
    ProbeScene probes({SetOf("Hit", 2, {{shake, 2}})}, NS::Vector3{0.0f, 1.0f, 4.0f});
    ASSERT_NE(probes.self, nullptr);
    ASSERT_TRUE(probes.Start(*probes.self, "Hit", NS::Vector3{}));
    probes.self->OverrideHitStop(5);
    probes.Step();
    EXPECT_NE(probes.self->ModelClassObj()->GetDrawOffset().x, 0.0f);
    probes.Step();
    EXPECT_EQ(probes.self->ModelClassObj()->GetDrawOffset().x, 0.0f);
    for (int frame = 0; frame < 3; ++frame)
    {
        probes.Step();
    }
    EXPECT_EQ(probes.self->stateSteps, 0);
    probes.Step();
    EXPECT_EQ(probes.self->stateSteps, 1);
}

// 沈みは同じ持ち主の「沈みを戻す」で跳ね返り、ほかの持ち主の戻しでは跳ね返らない
TEST(HitReactionStart, TheSinkBouncesOnlyOnItsOwnersRelease)
{
    NS::HitCameraSinkItem sink;
    sink.bottomPixels = 9.0f;
    sink.tremblePixels = 0.0f;
    const std::vector<NS::HitReactionSet> sets = {SetOf("Hit", 0, {{sink, 30}}),
                                                  SetOf("Rebound", 0, {{NS::HitCameraSinkReleaseItem{}, 1}})};
    ProbeScene probes(sets, NS::Vector3{0.0f, 1.0f, 4.0f});
    ASSERT_NE(probes.self, nullptr);
    ASSERT_TRUE(probes.Start(*probes.self, "Hit", NS::Vector3{}));
    for (int frame = 0; frame < 5; ++frame)
    {
        probes.Step();
    }
    EXPECT_FLOAT_EQ(probes.SinkPixels(), -9.0f);
    ASSERT_TRUE(probes.Start(*probes.other, "Rebound", NS::Vector3{}));
    probes.Step();
    probes.Step();
    EXPECT_FLOAT_EQ(probes.SinkPixels(), -9.0f);
    ASSERT_TRUE(probes.Start(*probes.self, "Rebound", NS::Vector3{}));
    probes.Step();
    EXPECT_FLOAT_EQ(probes.SinkPixels(), -9.0f);
    probes.Step();
    EXPECT_GT(probes.SinkPixels(), -9.0f + 0.1f);
}

// 戻されない沈みは底のまま、自分の長さで終わる
TEST(HitReactionStart, AnUnreleasedSinkEndsWithItsLength)
{
    NS::HitCameraSinkItem sink;
    sink.bottomPixels = 9.0f;
    sink.tremblePixels = 0.0f;
    ProbeScene probes({SetOf("Hit", 0, {{sink, 10}})}, NS::Vector3{0.0f, 1.0f, 4.0f});
    ASSERT_NE(probes.self, nullptr);
    ASSERT_TRUE(probes.Start(*probes.self, "Hit", NS::Vector3{}));
    // 始めたフレームを含めて 10 フレーム描く
    for (int frame = 0; frame < 10; ++frame)
    {
        probes.Step();
    }
    EXPECT_FLOAT_EQ(probes.SinkPixels(), -9.0f);
    probes.Step();
    EXPECT_EQ(probes.Cameras().FindModifier<NS::CameraSinkModifier>(), nullptr);
}

// トラウマは次の束に積む
TEST(HitReactionStart, TraumaStacksOnTheNextSet)
{
    NS::HitCameraTraumaItem trauma;
    trauma.trauma = 0.3f;
    // 2 回の間でほぼ減らさない。0 は終わらない揺れとして断られる
    trauma.decayPerSecond = 1.0e-6f;
    ProbeScene probes({SetOf("Miss", 0, {{trauma, 1}})}, NS::Vector3{0.0f, 1.0f, 4.0f});
    ASSERT_NE(probes.self, nullptr);
    ASSERT_TRUE(probes.Start(*probes.self, "Miss", NS::Vector3{}));
    probes.Step();
    EXPECT_NEAR(probes.Cameras().Trauma(), 0.3f, 1.0e-5f);
    ASSERT_TRUE(probes.Start(*probes.self, "Miss", NS::Vector3{}));
    EXPECT_NEAR(probes.Cameras().Trauma(), 0.6f, 1.0e-5f);
}

// 形の項目は、描く倍率を始めたフレームを 0 にした曲線の値で長さのフレーム数だけ変え、長さを過ぎると元へ戻す
// 相手へ向かう向きの倍率と横の倍率は、自機のセンサーから相手のセンサーへの水平の向きで x と z に分かれる
// 持ち主が書いた倍率に掛かり、止めの間も進む
TEST(HitReactionStart, TheShapeFollowsItsCurvesForItsLength)
{
    NS::HitShapeItem shape;
    shape.along = ConstantCurve(0.8f);
    shape.height.count = 2;
    shape.height.keys[0] = NS::Curve::Key{0.0f, 2.0f};
    shape.height.keys[1] = NS::Curve::Key{3.0f, 1.25f};
    shape.side = ConstantCurve(1.5f);
    const std::vector<NS::HitReactionSet> sets = {SetOf("Hit", 2, {{shape, 4}})};
    struct Case
    {
        NS::Vector3 otherPosition;
        NS::Vector3 first;
    };
    // 相手が前なら向きの倍率は z、横は x。相手が右なら逆
    for (const Case& c : {Case{NS::Vector3{0.0f, 1.0f, 4.0f}, NS::Vector3{1.5f, 2.0f, 0.8f}},
                          Case{NS::Vector3{4.0f, 1.0f, 0.0f}, NS::Vector3{0.8f, 2.0f, 1.5f}}})
    {
        SCOPED_TRACE(c.otherPosition.x);
        ProbeScene probes(sets, c.otherPosition);
        ASSERT_NE(probes.self, nullptr);
        NS::Model& model = *probes.self->ModelClassObj();
        ASSERT_TRUE(probes.Start(*probes.self, "Hit", NS::Vector3{}));
        probes.Step();
        EXPECT_FLOAT_EQ(model.GetDrawScale().x, c.first.x);
        EXPECT_FLOAT_EQ(model.GetDrawScale().y, c.first.y);
        EXPECT_FLOAT_EQ(model.GetDrawScale().z, c.first.z);
        probes.Step();
        EXPECT_FLOAT_EQ(model.GetDrawScale().y, 1.75f);
        // 持ち主の書いた倍率に掛ける
        ASSERT_TRUE(model.SetDrawScale(NS::Vector3{1.0f, 0.5f, 1.0f}));
        probes.Step();
        EXPECT_FLOAT_EQ(model.GetDrawScale().y, 1.5f * 0.5f);
        probes.Step();
        EXPECT_FLOAT_EQ(model.GetDrawScale().y, 1.25f * 0.5f);
        probes.Step();
        EXPECT_TRUE(model.GetDrawScale() == (NS::Vector3{1.0f, 0.5f, 1.0f}));
    }
}

// 形を止めると、描く倍率は持ち主の書いた倍率へその場で戻る
TEST(HitReactionStart, StoppingTheShapeRestoresTheDrawScale)
{
    NS::HitShapeItem shape;
    shape.height = ConstantCurve(2.0f);
    ProbeScene probes({SetOf("Hit", 0, {{shape, 10}})}, NS::Vector3{0.0f, 1.0f, 4.0f});
    ASSERT_NE(probes.self, nullptr);
    NS::Model& model = *probes.self->ModelClassObj();
    ASSERT_TRUE(probes.Start(*probes.self, "Hit", NS::Vector3{}));
    probes.Step();
    ASSERT_FLOAT_EQ(model.GetDrawScale().y, 2.0f);
    model.StopDrawShape();
    EXPECT_TRUE(model.GetDrawScale() == (NS::Vector3{1.0f, 1.0f, 1.0f}));
    probes.Step();
    EXPECT_TRUE(model.GetDrawScale() == (NS::Vector3{1.0f, 1.0f, 1.0f}));
}

// 床の波は項目の長さだけ、渡した位置の真下を中心に、始めたフレームを 0 にした曲線の半径と強さで広がり、長さの終わりで消える
TEST(HitReactionStart, GroundWaveSpreadsFromThePositionForItsLength)
{
    NS::HitGroundWaveItem wave;
    wave.radius.count = 2;
    wave.radius.keys[0] = NS::Curve::Key{0.0f, 0.0f};
    wave.radius.keys[1] = NS::Curve::Key{8.0f, 4.0f};
    wave.strength = ConstantCurve(0.8f);
    ProbeScene probes({SetOf("Hit", 3, {{wave, 8}})}, NS::Vector3{0.0f, 1.0f, 4.0f});
    ASSERT_NE(probes.self, nullptr);
    const NS::Vector3 contact{0.3f, 1.2f, 3.5f};
    ASSERT_TRUE(probes.Start(*probes.self, "Hit", contact));
    for (int frame = 0; frame < 10; ++frame)
    {
        SCOPED_TRACE(frame);
        probes.Step();
        const NS::Gfx::GroundWave& shown = probes.scene.GroundWaveShown();
        if (frame >= 8)
        {
            EXPECT_FLOAT_EQ(shown.strength, 0.0f);
            continue;
        }
        EXPECT_FLOAT_EQ(shown.centerX, contact.x);
        EXPECT_FLOAT_EQ(shown.centerZ, contact.z);
        EXPECT_FLOAT_EQ(shown.strength, 0.8f);
        EXPECT_FLOAT_EQ(shown.radius, 0.5f * static_cast<float>(frame));
    }
}

// 床の波を止めると消える。ほかの持ち主が止めても消えない
TEST(HitReactionStart, StoppingTheGroundWaveClearsOnlyTheOwnersWave)
{
    NS::HitGroundWaveItem wave;
    wave.radius = ConstantCurve(1.0f);
    wave.strength = ConstantCurve(0.8f);
    ProbeScene probes({SetOf("Hit", 0, {{wave, 10}})}, NS::Vector3{0.0f, 1.0f, 4.0f});
    ASSERT_NE(probes.self, nullptr);
    ASSERT_TRUE(probes.Start(*probes.self, "Hit", NS::Vector3{}));
    probes.Step();
    probes.other->HitReactionClassObj()->StopGroundWave();
    EXPECT_FLOAT_EQ(probes.scene.GroundWaveShown().strength, 0.8f);
    probes.self->HitReactionClassObj()->StopGroundWave();
    EXPECT_FLOAT_EQ(probes.scene.GroundWaveShown().strength, 0.0f);
    probes.Step();
    EXPECT_FLOAT_EQ(probes.scene.GroundWaveShown().strength, 0.0f);
}

// 持ち主が消えても、ほかの持ち主が頼んだカメラの効果は残る
TEST(HitReactionStart, AnOwnerLeavingKeepsTheOthersCameraEffects)
{
    NS::HitZoomRollItem zoom;
    zoom.zoom = 1.2f;
    zoom.returnFrames = 1;
    NS::HitCameraLurchItem lurch;
    lurch.distance = ConstantCurve(0.3f);
    ProbeScene probes({SetOf("Zoom", 0, {{zoom, 10}}), SetOf("Lurch", 0, {{lurch, 10}})},
                      NS::Vector3{0.0f, 1.0f, 4.0f});
    ASSERT_NE(probes.self, nullptr);
    ASSERT_TRUE(probes.Start(*probes.self, "Zoom", NS::Vector3{}));
    ASSERT_TRUE(probes.Start(*probes.other, "Lurch", NS::Vector3{}));
    probes.other->Disappear();
    EXPECT_FLOAT_EQ(probes.Cameras().ZoomRoll().zoom, 1.2f);
    EXPECT_EQ(probes.Cameras().FindModifier<NS::CameraNudgeModifier>(NS::CameraNudgeModifier::KindFor(false)),
              nullptr);
}
