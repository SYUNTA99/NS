#include "Editor/ActionTimelineEdit.h"
#include "Editor/HitPreview.h"
#include "NSlib/CoreObject/ClassObjs/ActionTimeline.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"

#include <gtest/gtest.h>

#include <vector>

// 動作のタイムラインの窓の、画面に依らない決まり (事象の足し引き・帯の範囲・始まった歩) を縛る

namespace
{
    // 0 フレーム目から 12 フレームの止め、13 フレーム目の白
    NS::ActorAction MakeTwoEvents()
    {
        NS::ActorAction action;
        action.name = "Test";
        action.events = {{NS::HitOthersStopItem{}, 0, 12}, {NS::HitFlashItem{}, 13, 1}};
        return action;
    }

    // 下見の 1 フレーム。動作 name が actionFrame を進め、started の事象が始まった姿
    NS::Editor::HitPreviewFrame MakeFrame(const char* name, int actionFrame, std::vector<std::size_t> started)
    {
        NS::Editor::HitPreviewFrame frame;
        frame.action = name;
        frame.actionFrame = actionFrame;
        frame.startedEvents = std::move(started);
        return frame;
    }
} // namespace

// 事象は再生の位置を始まりにして並びの最後へ足す。動作のフレームは状態に入った歩が 0 なので、負は 0 にする
TEST(EditorActionTimelineEdit, AddsAtThePlayheadAndStartsNoEarlierThanTheEntry)
{
    NS::ActorAction action = MakeTwoEvents();
    const std::size_t flash = NS::Editor::AddActionEvent(action, *NS::MakeHitReactionItem("Flash"), 5);
    ASSERT_EQ(flash, 2u);
    EXPECT_EQ(action.events[2].start, 5);
    EXPECT_EQ(action.events[2].length, 1);
    EXPECT_EQ(NS::HitReactionItemTypeName(action.events[2].item), "Flash");

    const std::size_t early = NS::Editor::AddActionEvent(action, *NS::MakeHitReactionItem("Shape"), -3);
    EXPECT_EQ(action.events[early].start, 0);
}

// 行を消すと後ろの行が詰まる。範囲の外は何もしない
TEST(EditorActionTimelineEdit, RemovesARow)
{
    NS::ActorAction action = MakeTwoEvents();
    EXPECT_FALSE(NS::Editor::RemoveActionEvent(action, 5));
    ASSERT_TRUE(NS::Editor::RemoveActionEvent(action, 0));
    ASSERT_EQ(action.events.size(), 1u);
    EXPECT_EQ(NS::HitReactionItemTypeName(action.events[0].item), "Flash");
}

// 帯の範囲は、事象の始まりと終わりと、動作に入った歩を 0 にした下見の記録の最初と最後のフレームを全部含む
TEST(EditorActionTimelineEdit, FrameRangeCoversEventsAndThePreviewFromTheEntry)
{
    const NS::ActorAction action = MakeTwoEvents();
    NS::Editor::TimelineFrameRange range = NS::Editor::ActionFrameRange(action, nullptr);
    EXPECT_EQ(range.first, 0);
    EXPECT_EQ(range.last, 13);

    // 途中のフレームから入った動作。記録の 4 番目のフレームで動作の 2 フレーム目を進めたので、
    // 0 フレーム目は記録の 2 番目に当たる。記録は 40 フレーム
    NS::Editor::HitPreviewResult preview;
    preview.hit = true;
    preview.detectionIndex = 10;
    preview.frames.resize(40);
    preview.frames[4] = MakeFrame("Test", 2, {0});
    preview.frames[5] = MakeFrame("Test", 3, {});
    range = NS::Editor::ActionFrameRange(action, &preview);
    EXPECT_EQ(range.first, -2);
    EXPECT_EQ(range.last, 37);

    // 下見でこの動作が走らなければ、事象だけ
    NS::ActorAction other = action;
    other.name = "Other";
    range = NS::Editor::ActionFrameRange(other, &preview);
    EXPECT_EQ(range.first, 0);
    EXPECT_EQ(range.last, 13);
}

// 行ごとに、下見で実際に始まったフレームを動作のフレームで返す。ほかの動作の行は数えない
TEST(EditorActionTimelineEdit, RowStartsCountFromTheEntry)
{
    NS::Editor::HitPreviewResult preview;
    preview.hit = true;
    preview.detectionIndex = 5;
    preview.frames.resize(8);
    // 途中のフレームから入った動作。最初に進めたフレームが 3
    preview.frames[1] = MakeFrame("Test", 3, {0});
    preview.frames[2] = MakeFrame("Test", 4, {1, 0});
    preview.frames[3] = MakeFrame("Other", 0, {1});
    EXPECT_EQ(NS::Editor::RowStartFrames(preview, "Test", 0), (std::vector<int>{3, 4}));
    EXPECT_EQ(NS::Editor::RowStartFrames(preview, "Test", 1), (std::vector<int>{4}));
    EXPECT_TRUE(NS::Editor::RowStartFrames(preview, "Test", 2).empty());
    EXPECT_EQ(NS::Editor::RowStartFrames(preview, "Other", 1), (std::vector<int>{0}));
}
