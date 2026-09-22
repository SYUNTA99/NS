#include "Game/Level/GoalComponent.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Scene/SceneData.h"

#include <gtest/gtest.h>

namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Obj;

namespace
{
    SceneNs::ObjectData MakeGoalObject()
    {
        SceneNs::ObjectData object{};
        object.components.push_back(SceneNs::MakeComponentEntry("GoalComponent"));
        return object;
    }
} // namespace

// 共有アクセサ FindComponentEntry / HasField / IsGoalObject の挙動 (発見 / 不在) を縛る
// 編集とプレイ進行が同じアクセサを読むので、ここが種別判定の唯一の判定点になる
TEST(SceneDataAccessors, FindComponentFieldAndGoalRule)
{
    SceneNs::ObjectData goal = MakeGoalObject();

    const nlohmann::json* entry = SceneNs::FindComponentEntry(goal, "GoalComponent");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(SceneNs::FindComponentEntry(goal, "BoxColliderComponent"), nullptr); // 不在は nullptr

    nlohmann::json box = SceneNs::MakeComponentEntry("BoxColliderComponent");
    SceneNs::SetField(box, "半径", NS::Core::Vector3{1.0f, 1.0f, 1.0f});
    EXPECT_TRUE(SceneNs::HasField(box, "半径"));
    EXPECT_FALSE(SceneNs::HasField(box, "Missing")); // 欠損 field は無し

    EXPECT_TRUE(LevelNs::IsGoalObject(goal));
    EXPECT_FALSE(LevelNs::IsGoalObject(SceneNs::ObjectData{})); // GoalComponent 無し
}

TEST(SceneDataComponents, ComponentEntryHoldsTypeAndFields)
{
    nlohmann::json entry = SceneNs::MakeComponentEntry("BoxColliderComponent");
    SceneNs::SetField(entry, "半径", NS::Core::Vector3{0.5f, 0.5f, 0.5f});
    SceneNs::SetField(entry, "中心オフセット", NS::Core::Vector3{0.0f, 1.0f, 0.0f});

    EXPECT_EQ(SceneNs::ComponentEntryType(entry), "BoxColliderComponent");
    ASSERT_EQ(entry.at("fields").size(), 2u);
    EXPECT_TRUE(SceneNs::HasField(entry, "半径"));
    EXPECT_TRUE(SceneNs::HasField(entry, "中心オフセット"));
}

TEST(SceneDataComponents, ObjectDataCopyIsDeep)
{
    SceneNs::ObjectData a{};
    a.components.push_back(SceneNs::MakeComponentEntry("HazardComponent"));

    SceneNs::ObjectData b = a;
    b.components.clear();

    EXPECT_EQ(a.components.size(), 1u);
    EXPECT_EQ(b.components.size(), 0u);
}
