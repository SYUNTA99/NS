#include "Editor/ActionTimelineEdit.h"
#include "NSlib/CoreObject/Action/ActorAction.h"
#include "NSlib/CoreObject/Reflection/ItemVariant.h"

#include <gtest/gtest.h>

// 動作のタイムラインの窓の、画面に依らない決まり (事象の足し引き・帯の範囲) を縛る

namespace
{
    // 0 フレーム目から 12 フレームの止め、13 フレーム目の白
    NS::ActorActionInfo MakeTwoEvents()
    {
        NS::ActorActionInfo action;
        action.name = "Test";
        action.events = {{NS::ActionPadItem{}, 0, 12}, {NS::ActionScreenItem{}, 13, 1}};
        return action;
    }
} // namespace

// 事象は再生の位置を始まりにして並びの最後へ足す。動作のフレームは状態に入った歩が 0 なので、負は 0 にする
TEST(EditorActionTimelineEdit, AddsAtThePlayheadAndStartsNoEarlierThanTheEntry)
{
    NS::ActorActionInfo action = MakeTwoEvents();
    const std::size_t screen = NS::Editor::AddActionEvent(action, *NS::MakeItem<NS::ActionItem>("Screen"), 5);
    ASSERT_EQ(screen, 2u);
    EXPECT_EQ(action.events[2].start, 5);
    EXPECT_EQ(action.events[2].length, 1);
    EXPECT_EQ(NS::ItemTypeName(action.events[2].item), "Screen");

    const std::size_t early = NS::Editor::AddActionEvent(action, *NS::MakeItem<NS::ActionItem>("Sound"), -3);
    EXPECT_EQ(action.events[early].start, 0);
}

// 行を消すと後ろの行が詰まる。範囲の外は何もしない
TEST(EditorActionTimelineEdit, RemovesARow)
{
    NS::ActorActionInfo action = MakeTwoEvents();
    EXPECT_FALSE(NS::Editor::RemoveActionEvent(action, 5));
    ASSERT_TRUE(NS::Editor::RemoveActionEvent(action, 0));
    ASSERT_EQ(action.events.size(), 1u);
    EXPECT_EQ(NS::ItemTypeName(action.events[0].item), "Screen");
}

// 帯の範囲は、事象の始まりと終わりを全部含む
TEST(EditorActionTimelineEdit, FrameRangeCoversEvents)
{
    const NS::ActorActionInfo action = MakeTwoEvents();
    const NS::Editor::TimelineFrameRange range = NS::Editor::ActionFrameRange(action);
    EXPECT_EQ(range.first, 0);
    EXPECT_EQ(range.last, 13);
}

// 動作の長さを決めた動作は、事象の並びより長ければ長さの終わりまでを含む
TEST(EditorActionTimelineEdit, FrameRangeCoversTheActionLength)
{
    NS::ActorActionInfo dash;
    dash.name = "AirDash";
    dash.length = 29;
    NS::Editor::TimelineFrameRange range = NS::Editor::ActionFrameRange(dash);
    EXPECT_EQ(range.first, 0);
    EXPECT_EQ(range.last, 28);

    NS::ActorActionInfo shorter = MakeTwoEvents();
    shorter.length = 5;
    range = NS::Editor::ActionFrameRange(shorter);
    EXPECT_EQ(range.first, 0);
    EXPECT_EQ(range.last, 13);
}
