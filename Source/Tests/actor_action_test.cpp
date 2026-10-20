#include "NSlib/CoreObject/ClassObjs/ActionTimeline.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "TestActorFile.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

// 動作のタイムラインのファイルの形・読み込みと保存の往復・壊れた動作の扱いを縛る

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

TEST(ActorAction, MissingFieldsKeepTheDefaultsAndUnknownFieldsAreSkipped)
{
    const nlohmann::json doc = nlohmann::json::parse(R"({"name": "A", "古い欄": 0, "events": [
        {"type": "Flash", "start": 1, "length": 6, "fields": {"濃さ": 0.3, "消した欄": 2}},
        {"type": "ZoomRoll", "start": 2, "length": 7}
    ]})");
    std::string error;
    const TestLogCapture log;
    const std::optional<NS::ActorAction> read = NS::ParseActorAction(doc, error);
    ASSERT_TRUE(read.has_value()) << error;
    ASSERT_EQ(read->events.size(), 2u);
    EXPECT_FLOAT_EQ(std::get<NS::HitFlashItem>(read->events[0].item).alpha, 0.3f);
    EXPECT_FLOAT_EQ(std::get<NS::HitZoomRollItem>(read->events[1].item).zoom, NS::HitZoomRollItem{}.zoom);
    EXPECT_EQ(log.WarningCount(), 2);
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
