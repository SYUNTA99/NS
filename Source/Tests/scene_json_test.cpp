#include "Game/Level/Goal.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/ObjectJson.h"

#include <gtest/gtest.h>

namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Obj;

namespace
{
    nlohmann::json MakeGoalObject()
    {
        nlohmann::json object = SceneNs::MakeObjectJson();
        SceneNs::ObjectJsonComponents(object).push_back(SceneNs::MakeComponentEntry("Goal"));
        return object;
    }
} // namespace

// 共有アクセサ FindComponentEntry / HasField / IsGoalObject の挙動 (発見 / 不在) を縛る
// 編集とプレイ進行が同じアクセサを読むので、ここが種別判定の唯一の判定点になる
TEST(SceneJsonAccessors, FindComponentFieldAndGoalRule)
{
    nlohmann::json goal = MakeGoalObject();

    const nlohmann::json* entry = SceneNs::FindComponentEntry(goal, "Goal");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(SceneNs::FindComponentEntry(goal, "BoxCollider"), nullptr); // 不在は nullptr

    nlohmann::json box = SceneNs::MakeComponentEntry("BoxCollider");
    SceneNs::SetField(box, "半径", NS::Core::Vector3{1.0f, 1.0f, 1.0f});
    EXPECT_TRUE(SceneNs::HasField(box, "半径"));
    EXPECT_FALSE(SceneNs::HasField(box, "Missing")); // 欠損 field は無し

    EXPECT_TRUE(LevelNs::IsGoalObject(goal));
    EXPECT_FALSE(LevelNs::IsGoalObject(SceneNs::MakeObjectJson())); // Goal 無し
}

TEST(SceneJsonComponents, ComponentEntryHoldsTypeAndFields)
{
    nlohmann::json entry = SceneNs::MakeComponentEntry("BoxCollider");
    SceneNs::SetField(entry, "半径", NS::Core::Vector3{0.5f, 0.5f, 0.5f});
    SceneNs::SetField(entry, "中心オフセット", NS::Core::Vector3{0.0f, 1.0f, 0.0f});

    EXPECT_EQ(SceneNs::ComponentEntryType(entry), "BoxCollider");
    ASSERT_EQ(entry.at("fields").size(), 2u);
    EXPECT_TRUE(SceneNs::HasField(entry, "半径"));
    EXPECT_TRUE(SceneNs::HasField(entry, "中心オフセット"));
}

// 配置物の JSON のコピーは components まで別物になる
TEST(SceneJsonComponents, ObjectJsonCopyIsDeep)
{
    nlohmann::json a = SceneNs::MakeObjectJson();
    SceneNs::ObjectJsonComponents(a).push_back(SceneNs::MakeComponentEntry("Goal"));

    nlohmann::json b = a;
    SceneNs::ObjectJsonComponents(b).clear();

    EXPECT_EQ(SceneNs::ObjectJsonComponents(a).size(), 1u);
    EXPECT_EQ(SceneNs::ObjectJsonComponents(b).size(), 0u);
}
