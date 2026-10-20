#include "NSlib/CoreObject/ClassObjs/ActionTimeline.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/Windows/FileSystem.h"
#include "TestActorFile.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <variant>
#include <vector>

// 名前の束の項目の型・束のファイルの形・読み込みと保存の往復・壊れた束の扱いを縛る

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
