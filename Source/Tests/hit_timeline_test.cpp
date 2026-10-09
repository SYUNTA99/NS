#include "Game/Level/HitTimeline.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Windows/FileSystem.h"
#include "Tests/TestHitTimelines.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <variant>

// 当たりのタイムラインのファイルの形・読み込みと保存の往復・壊れたファイルの扱いを縛る

namespace
{
    using GL::Level::HitTimeline;
    using GL::Level::HitTimelineLibrary;

    // 種類ごとに 1 つずつ、欄と向きを既定から動かした並び
    HitTimeline MakeEveryKindTimeline()
    {
        HitTimeline timeline;
        GL::Level::ShapeEvent shape;
        shape.along.count = 2;
        shape.along.keys[0] = NS::Obj::Curve::Key{0.0f, 0.7f};
        shape.along.keys[1] = NS::Obj::Curve::Key{11.0f, 0.7f};
        shape.height.count = 1;
        shape.height.keys[0] = NS::Obj::Curve::Key{0.0f, 1.1f};
        GL::Level::TargetFreezeEvent freeze;
        freeze.pushInDistance = 0.1f;
        GL::Level::CameraShakeEvent shake;
        shake.sideWeight = 1.0f;
        shake.longestFlipFrames = 3;
        GL::Level::ZoomRollEvent zoom;
        zoom.zoom = 1.3f;
        GL::Level::PadVibrationEvent pad;
        pad.right.count = 2;
        pad.right.keys[0] = NS::Obj::Curve::Key{0.0f, 0.6f};
        pad.right.keys[1] = NS::Obj::Curve::Key{16.0f, 0.0f};
        GL::Level::FlashEvent flash;
        flash.alpha = 0.25f;
        GL::Level::CameraLurchEvent lurch;
        lurch.distance.count = 2;
        lurch.distance.keys[0] = NS::Obj::Curve::Key{0.0f, 0.3f};
        lurch.distance.keys[1] = NS::Obj::Curve::Key{8.0f, 0.0f};
        GL::Level::GroundWaveEvent wave;
        wave.radius.count = 2;
        wave.radius.keys[0] = NS::Obj::Curve::Key{0.0f, 0.5f};
        wave.radius.keys[1] = NS::Obj::Curve::Key{10.0f, 6.0f};
        GL::Level::DistortionRingEvent ring;
        ring.halfWidth = 0.07f;
        GL::Level::ShakeLinesEvent lines;
        lines.gapPixels = 20.0f;
        GL::Level::CameraReboundSwayEvent sway;
        sway.distance.count = 2;
        sway.distance.keys[0] = NS::Obj::Curve::Key{0.0f, -0.5f};
        sway.distance.keys[1] = NS::Obj::Curve::Key{12.0f, 0.8f};
        timeline.events = {
            {GL::Level::HitStopEvent{}, 1, 12, GL::Level::HitDirection::Any},
            {shape, -6, 18, GL::Level::HitDirection::Any},
            {freeze, 1, 12, GL::Level::HitDirection::Any},
            {GL::Level::TargetLaunchEvent{}, 13, 1, GL::Level::HitDirection::Any},
            {GL::Level::ReboundEvent{}, 13, 1, GL::Level::HitDirection::Any},
            {shake, 1, 16, GL::Level::HitDirection::Left},
            {zoom, 1, 18, GL::Level::HitDirection::Any},
            {pad, 1, 16, GL::Level::HitDirection::Right},
            {flash, 1, 6, GL::Level::HitDirection::Up},
            {GL::Level::HitEffectEvent{}, 1, 1, GL::Level::HitDirection::Down},
            {GL::Level::FlightEffectEvent{}, 13, 0, GL::Level::HitDirection::Any},
            {lurch, 1, 9, GL::Level::HitDirection::Any},
            {sway, 1, 29, GL::Level::HitDirection::Right},
            {lines, 1, 12, GL::Level::HitDirection::Any},
            {wave, 1, 18, GL::Level::HitDirection::Any},
            {ring, 2, 10, GL::Level::HitDirection::Any},
            {GL::Level::CameraTraumaEvent{}, 1, 12, GL::Level::HitDirection::Any},
            {GL::Level::GradualReleaseEvent{}, 1, 12, GL::Level::HitDirection::Any},
            {GL::Level::ImpactTremorEvent{}, 1, 12, GL::Level::HitDirection::Any},
            {GL::Level::CameraSinkEvent{}, 1, 12, GL::Level::HitDirection::Any},
            {GL::Level::OthersStopEvent{}, 1, 12, GL::Level::HitDirection::Any},
            {GL::Level::BodyShakeEvent{}, 1, 12, GL::Level::HitDirection::Any},
        };
        return timeline;
    }
} // namespace

TEST(HitTimeline, ValueTypeFieldsRoundTripThroughJson)
{
    GL::Level::CameraShakeEvent shake;
    shake.strength = 0.2f;
    shake.longestFlipFrames = 4;
    const nlohmann::json fields = NS::Obj::SerializeValueFields(shake);
    EXPECT_FLOAT_EQ(fields["強さ"].get<float>(), 0.2f);
    EXPECT_EQ(fields["入れ替わりの最長フレーム数"].get<int>(), 4);

    GL::Level::CameraShakeEvent read;
    EXPECT_EQ(NS::Obj::ApplyValueFields(read, fields), 0u);
    EXPECT_FLOAT_EQ(read.strength, 0.2f);
    EXPECT_EQ(read.longestFlipFrames, 4);
}

TEST(HitTimeline, EveryKindRoundTripsThroughTheFileForm)
{
    const HitTimeline written = MakeEveryKindTimeline();
    ASSERT_EQ(written.events.size(), std::variant_size_v<GL::Level::HitEventValue>);
    const nlohmann::json doc = GL::Level::HitTimelineToJson(written);
    EXPECT_EQ(doc["version"].get<int>(), 1);
    ASSERT_EQ(doc["events"].size(), written.events.size());
    EXPECT_EQ(doc["events"][0]["type"].get<std::string>(), "HitStop");
    EXPECT_EQ(doc["events"][5]["direction"].get<std::string>(), "left");

    std::string error;
    const std::optional<HitTimeline> read = GL::Level::ParseHitTimeline(doc, error);
    ASSERT_TRUE(read.has_value()) << error;
    ASSERT_EQ(read->events.size(), written.events.size());
    for (std::size_t i = 0; i < written.events.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(read->events[i].value.index(), written.events[i].value.index());
        EXPECT_EQ(read->events[i].start, written.events[i].start);
        EXPECT_EQ(read->events[i].length, written.events[i].length);
        EXPECT_EQ(read->events[i].direction, written.events[i].direction);
    }
    EXPECT_EQ(GL::Level::HitTimelineToJson(*read), doc);
    const GL::Level::ShapeEvent* shape = std::get_if<GL::Level::ShapeEvent>(&read->events[1].value);
    ASSERT_NE(shape, nullptr);
    EXPECT_EQ(shape->along, std::get<GL::Level::ShapeEvent>(written.events[1].value).along);
}

TEST(HitTimeline, MissingFieldsKeepTheDefaultsAndUnknownFieldsAreSkipped)
{
    const nlohmann::json doc = nlohmann::json::parse(R"({"version": 1, "events": [
        {"type": "Flash", "start": 1, "length": 6, "fields": {"濃さ": 0.3, "消した欄": 2}},
        {"type": "CameraShake", "start": 1, "length": 12}
    ]})");
    std::string error;
    const std::optional<HitTimeline> read = GL::Level::ParseHitTimeline(doc, error);
    ASSERT_TRUE(read.has_value()) << error;
    ASSERT_EQ(read->events.size(), 2u);
    EXPECT_FLOAT_EQ(std::get<GL::Level::FlashEvent>(read->events[0].value).alpha, 0.3f);
    EXPECT_EQ(read->events[0].direction, GL::Level::HitDirection::Any);
    EXPECT_FLOAT_EQ(std::get<GL::Level::CameraShakeEvent>(read->events[1].value).strength,
                    GL::Level::CameraShakeEvent{}.strength);
}

TEST(HitTimeline, BrokenFilesAreRejectedWithAReason)
{
    const char* const broken[] = {
        R"({"version": 2, "events": []})",
        R"({"version": 1})",
        R"({"version": 1, "events": [{"type": "Explode", "start": 1, "length": 1}]})",
        R"({"version": 1, "events": [{"type": "Flash", "start": 1, "length": -1}]})",
        R"({"version": 1, "events": [{"type": "Flash", "start": 1.5, "length": 1}]})",
        R"({"version": 1, "events": [{"type": "Flash", "start": 1, "length": 1, "direction": "back"}]})",
        R"({"version": 1, "events": [{"type": "Flash", "length": 1}]})",
        // 触れる前に置けるのは自機の形だけ。止めや相手への知らせは当たりの結果が要る
        R"({"version": 1, "events": [{"type": "HitStop", "start": -1, "length": 1}]})",
        R"({"version": 1, "events": [{"type": "Flash", "start": -3, "length": 2}]})",
        R"([1, 2])",
    };
    for (const char* text : broken)
    {
        SCOPED_TRACE(text);
        std::string error;
        EXPECT_FALSE(GL::Level::ParseHitTimeline(nlohmann::json::parse(text), error).has_value());
        EXPECT_FALSE(error.empty());
    }
}

TEST(HitTimeline, TiersNameTheirFiles)
{
    EXPECT_EQ(GL::Level::HitTimelineNameOf(GL::Level::HitTier::Center), "center");
    EXPECT_EQ(GL::Level::HitTimelineNameOf(GL::Level::HitTier::Wide), "miss");
    EXPECT_TRUE(GL::Level::HitTimelineNameOf(static_cast<GL::Level::HitTier>(7)).empty());
}

TEST(HitTimeline, LibrarySavesAndReadsBackTheSameTimeline)
{
    const ScopedHitTimelineDirectory directory("RoundTrip");
    const HitTimeline written = MakeEveryKindTimeline();
    HitTimelineLibrary::Get().Set("center", written);
    ASSERT_TRUE(HitTimelineLibrary::Get().Save("center"));
    HitTimelineLibrary::Get().Reload();
    const HitTimeline* read = HitTimelineLibrary::Get().FindForTier(GL::Level::HitTier::Center);
    ASSERT_NE(read, nullptr);
    EXPECT_EQ(GL::Level::HitTimelineToJson(*read), GL::Level::HitTimelineToJson(written));
    EXPECT_FALSE(HitTimelineLibrary::Get().Save("graze"));
}

TEST(HitTimeline, LibraryHasNoTimelineForAMissingOrBrokenFile)
{
    const ScopedHitTimelineDirectory directory("Broken");
    directory.WriteFile("center", R"({"version": 1, "events": [{"type": "Explode", "start": 1, "length": 1}]})");
    directory.WriteFile("miss", "{ not json");
    HitTimelineLibrary::Get().Reload();
    EXPECT_EQ(HitTimelineLibrary::Get().FindForTier(GL::Level::HitTier::Center), nullptr);
    EXPECT_EQ(HitTimelineLibrary::Get().FindForTier(GL::Level::HitTier::Wide), nullptr);
    EXPECT_EQ(HitTimelineLibrary::Get().FindForTier(static_cast<GL::Level::HitTier>(7)), nullptr);
    EXPECT_EQ(HitTimelineLibrary::Get().Find("graze"), nullptr);
}

namespace
{
    // 出荷の Assets/HitTimelines/<name>.json を読む。読めなければ空
    std::optional<HitTimeline> ReadShippedTimeline(std::string_view name)
    {
        using NS::OS::FileSystem;
        const std::string path = FileSystem::Combine(
            FileSystem::Combine(FileSystem::Combine(FileSystem::ContentRoot(), "Assets"), "HitTimelines"),
            std::string{name} + ".json");
        const std::optional<std::string> text = FileSystem::ReadAllText(path);
        if (!text.has_value())
        {
            return std::nullopt;
        }
        std::string error;
        return GL::Level::ParseHitTimeline(nlohmann::json::parse(*text), error);
    }
} // namespace

// 真ん中は触れる 6 フレーム前から突進の向きに縮み、触れた次のフレームにもっと深く潰れる
// 触れる 2 フレーム前から触れるまで自機以外を止める。外れは触れる前に何も起こさない
TEST(HitTimeline, ShippedCenterShrinksBeforeContactAndMissDoesNot)
{
    const std::optional<HitTimeline> center = ReadShippedTimeline("center");
    ASSERT_TRUE(center.has_value());
    const GL::Level::HitEvent* shape = nullptr;
    const GL::Level::HitEvent* othersStop = nullptr;
    for (const GL::Level::HitEvent& event : center->events)
    {
        if (std::holds_alternative<GL::Level::ShapeEvent>(event.value))
        {
            shape = &event;
        }
        if (std::holds_alternative<GL::Level::OthersStopEvent>(event.value))
        {
            othersStop = &event;
        }
    }
    ASSERT_NE(shape, nullptr);
    ASSERT_NE(othersStop, nullptr);
    EXPECT_EQ(shape->start, -6);
    EXPECT_EQ(othersStop->start, -2);
    EXPECT_EQ(othersStop->length, 2);
    const NS::Obj::Curve& along = std::get<GL::Level::ShapeEvent>(shape->value).along;
    // 横軸は形の始まり (-6) からのフレーム数
    const float atStart = along.Evaluate(0.0f);
    const float halfway = along.Evaluate(3.0f);
    const float atContact = along.Evaluate(6.0f);
    const float atStopHead = along.Evaluate(7.0f);
    EXPECT_FLOAT_EQ(atStart, 1.0f);
    // 初めはわずかで、触れる直前にぐっと縮む
    EXPECT_LT(1.0f - halfway, (1.0f - atContact) * 0.5f);
    EXPECT_NEAR(atContact, 0.85f, 1.0e-4f);
    EXPECT_LT(atStopHead, atContact);

    const std::optional<HitTimeline> miss = ReadShippedTimeline("miss");
    ASSERT_TRUE(miss.has_value());
    for (const GL::Level::HitEvent& event : miss->events)
    {
        EXPECT_GE(event.start, 0) << GL::Level::HitEventTypeName(event.value);
    }
}

// 真ん中の揺れは止めの頭から沈む揺れ 1 つ。平行移動の揺れと寄りは外した
// 出荷の真ん中は、紫で当てた時だけ止めの明けのフレームから世界を遅くする
TEST(HitTimeline, ShippedCenterSlowsTheWorldOnlyForPurple)
{
    const std::optional<HitTimeline> center = ReadShippedTimeline("center");
    ASSERT_TRUE(center.has_value());
    int stopEnd = 0;
    int releases = 0;
    for (const GL::Level::HitEvent& event : center->events)
    {
        if (std::holds_alternative<GL::Level::HitStopEvent>(event.value))
        {
            stopEnd = event.start + event.length;
        }
    }
    for (const GL::Level::HitEvent& event : center->events)
    {
        if (const GL::Level::GradualReleaseEvent* release = std::get_if<GL::Level::GradualReleaseEvent>(&event.value))
        {
            ++releases;
            EXPECT_TRUE(release->overchargedOnly);
            EXPECT_EQ(event.start, stopEnd);
        }
    }
    EXPECT_EQ(releases, 1);
    const std::optional<HitTimeline> miss = ReadShippedTimeline("miss");
    ASSERT_TRUE(miss.has_value());
    for (const GL::Level::HitEvent& event : miss->events)
    {
        EXPECT_FALSE(std::holds_alternative<GL::Level::GradualReleaseEvent>(event.value));
    }
}

// 真ん中のカメラは沈む揺れが 1 つと、衝撃が背中に着く止めの 7 フレーム目に 1 回だけの傾かない寄り (押し)
TEST(HitTimeline, ShippedCenterSinksAndPunchesOnceWhenTheShockReachesTheBack)
{
    const std::optional<HitTimeline> center = ReadShippedTimeline("center");
    ASSERT_TRUE(center.has_value());
    int sinks = 0;
    int punches = 0;
    for (const GL::Level::HitEvent& event : center->events)
    {
        EXPECT_FALSE(std::holds_alternative<GL::Level::CameraShakeEvent>(event.value));
        if (const GL::Level::ZoomRollEvent* zoom = std::get_if<GL::Level::ZoomRollEvent>(&event.value))
        {
            ++punches;
            EXPECT_EQ(event.start, 7);
            EXPECT_FLOAT_EQ(zoom->rollDegrees, 0.0f);
            EXPECT_GT(zoom->zoom, 1.0f);
        }
        if (std::holds_alternative<GL::Level::CameraSinkEvent>(event.value))
        {
            ++sinks;
            EXPECT_EQ(event.start, 1);
            // 揺れのフレーム数の上限 60 に収める
            EXPECT_LE(event.length, 60);
        }
    }
    EXPECT_EQ(sinks, 1);
    EXPECT_EQ(punches, 1);
}

// 外れの振動は向きごとに 1 行ずつ。頭に重い方 (左) の一打を 2〜3 フレーム鳴らしてすぐ切り、軽い方 (右) の擦れが抜ける
// 下の外れだけ、擦れの頭にもう 1 度重い一打が来る。向きの付かない振動の行は置かない
TEST(HitTimeline, ShippedMissVibratesHeavyHeadThenLightTailPerDirection)
{
    using GL::Level::HitDirection;
    const std::optional<HitTimeline> miss = ReadShippedTimeline("miss");
    ASSERT_TRUE(miss.has_value());
    int rows[5] = {};
    for (const GL::Level::HitEvent& event : miss->events)
    {
        const GL::Level::PadVibrationEvent* pad = std::get_if<GL::Level::PadVibrationEvent>(&event.value);
        if (pad == nullptr)
        {
            continue;
        }
        ++rows[static_cast<int>(event.direction)];
        SCOPED_TRACE(static_cast<int>(event.direction));
        // 頭の一打: 重い方が鳴り、軽い方は鳴らない
        EXPECT_GT(pad->left.Evaluate(0.0f), 0.5f);
        EXPECT_FLOAT_EQ(pad->right.Evaluate(0.0f), 0.0f);
        // 一打は 3 フレーム目までに切れ、擦れは軽い方
        EXPECT_GT(pad->right.Evaluate(4.0f), pad->left.Evaluate(5.0f));
        EXPECT_FLOAT_EQ(pad->left.Evaluate(8.0f), 0.0f);
        // 長さで 0 へ抜ける
        EXPECT_FLOAT_EQ(pad->right.Evaluate(static_cast<float>(event.length)), 0.0f);
        if (event.direction == HitDirection::Down)
        {
            EXPECT_GT(pad->left.Evaluate(4.0f), pad->left.Evaluate(3.0f));
        }
    }
    EXPECT_EQ(rows[static_cast<int>(HitDirection::Any)], 0);
    EXPECT_EQ(rows[static_cast<int>(HitDirection::Right)], 1);
    EXPECT_EQ(rows[static_cast<int>(HitDirection::Left)], 1);
    EXPECT_EQ(rows[static_cast<int>(HitDirection::Up)], 1);
    EXPECT_EQ(rows[static_cast<int>(HitDirection::Down)], 1);
}
