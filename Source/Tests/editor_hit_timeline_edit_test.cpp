#include "Editor/HitPreview.h"
#include "Editor/HitTimelineEdit.h"
#include "Game/Level/HitTimeline.h"

#include <gtest/gtest.h>

#include <vector>

// タイムラインのパネルの、画面に依らない決まり (事象の足し引き・帯の範囲・始まりの重ね・再生の進み) を縛る

namespace
{
    using namespace GL::Level;

    HitTimeline MakeTwoEvents()
    {
        HitTimeline timeline;
        timeline.events = {{HitStopEvent{}, 1, 12, HitDirection::Any}, {ReboundEvent{}, 13, 1, HitDirection::Any}};
        return timeline;
    }
} // namespace

// 事象は再生の位置を始まりにして並びの最後へ足す。触れる前に置けない種類は 0 から始める
TEST(EditorHitTimelineEdit, AddsAtThePlayheadAndKeepsTheBeforeContactRule)
{
    HitTimeline timeline = MakeTwoEvents();
    const std::size_t flash = NS::Editor::AddHitEvent(timeline, *MakeHitEventValue("Flash"), 5);
    ASSERT_EQ(flash, 2u);
    EXPECT_EQ(timeline.events[2].start, 5);
    EXPECT_EQ(HitEventTypeName(timeline.events[2].value), "Flash");

    const std::size_t shape = NS::Editor::AddHitEvent(timeline, *MakeHitEventValue("Shape"), -3);
    EXPECT_EQ(timeline.events[shape].start, -3);
    const std::size_t stop = NS::Editor::AddHitEvent(timeline, *MakeHitEventValue("HitStop"), -3);
    EXPECT_EQ(timeline.events[stop].start, 0);
}

// 行を消すと後ろの行が詰まる。範囲の外は何もしない
TEST(EditorHitTimelineEdit, RemovesARow)
{
    HitTimeline timeline = MakeTwoEvents();
    EXPECT_FALSE(NS::Editor::RemoveHitEvent(timeline, 5));
    ASSERT_TRUE(NS::Editor::RemoveHitEvent(timeline, 0));
    ASSERT_EQ(timeline.events.size(), 1u);
    EXPECT_EQ(HitEventTypeName(timeline.events[0].value), "Rebound");
}

// 帯の範囲は、事象の始まりと終わりと、下見の記録の最初と最後のフレームを全部含む
TEST(EditorHitTimelineEdit, FrameRangeCoversEventsAndThePreview)
{
    HitTimeline timeline = MakeTwoEvents();
    NS::Editor::TimelineFrameRange range = NS::Editor::HitTimelineFrameRange(timeline, nullptr);
    EXPECT_EQ(range.first, 0);
    EXPECT_EQ(range.last, 13);

    NS::Editor::HitPreviewResult preview;
    preview.hit = true;
    preview.detectionIndex = 10;
    preview.frames.resize(40);
    range = NS::Editor::HitTimelineFrameRange(timeline, &preview);
    EXPECT_EQ(range.first, -10);
    EXPECT_EQ(range.last, 29);
}

// 行ごとに、下見で実際に始まったフレームを検知のフレームから数えて返す
TEST(EditorHitTimelineEdit, RowStartsCountFromTheDetection)
{
    NS::Editor::HitPreviewResult preview;
    preview.hit = true;
    preview.detectionIndex = 2;
    preview.frames.resize(6);
    preview.frames[2].startedRows = {0};
    preview.frames[3].startedRows = {1, 0};
    EXPECT_EQ(NS::Editor::RowStartFrames(preview, 0), (std::vector<int>{0, 1}));
    EXPECT_EQ(NS::Editor::RowStartFrames(preview, 1), (std::vector<int>{1}));
    EXPECT_TRUE(NS::Editor::RowStartFrames(preview, 2).empty());
}
