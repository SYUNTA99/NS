#include "Game/Level/BlockObject.h"
#include "Game/Level/GoalComponent.h"
#include "Runtime/Math/Math.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"

#include <gtest/gtest.h>

namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Object;

namespace
{
    // ゴールの印だけを持つ object を作る。 意味は components が表すので CRC も components で決まる
    SceneNs::ObjectData MakeGoalObject()
    {
        SceneNs::ObjectData object{};
        object.components.push_back(SceneNs::MakeComponentEntry("GoalComponent"));
        return object;
    }
} // namespace

TEST(SceneDataCrcTest, EmptyLevelIsDeterministic)
{
    SceneNs::SceneData a, b;
    EXPECT_EQ(a.ComputeCrc32(), b.ComputeCrc32());
}

// 共有アクセサ FindComponentEntry / HasField / IsGoalObject の挙動 (発見 / 不在) を縛る
// 編集とプレイ進行が同じアクセサを読むので、 ここが種別判定の唯一の判定点になる
TEST(SceneDataAccessors, FindComponentFieldAndGoalRule)
{
    SceneNs::ObjectData goal = MakeGoalObject();

    const nlohmann::json* entry = SceneNs::FindComponentEntry(goal, "GoalComponent");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(SceneNs::FindComponentEntry(goal, "BoxColliderComponent"), nullptr); // 不在は nullptr

    nlohmann::json box = SceneNs::MakeComponentEntry("BoxColliderComponent");
    SceneNs::SetField(box, "Half Extents", NS::Math::Vector3{1.0f, 1.0f, 1.0f});
    EXPECT_TRUE(SceneNs::HasField(box, "Half Extents"));
    EXPECT_FALSE(SceneNs::HasField(box, "Missing")); // 欠損 field は無し

    EXPECT_TRUE(LevelNs::IsGoalObject(goal));
    EXPECT_FALSE(LevelNs::IsGoalObject(SceneNs::ObjectData{})); // GoalComponent 無し
}

TEST(SceneDataCrcTest, DifferentComponentsProduceDifferentCrc)
{
    // 同じ component 型でも field 値が違えば CRC が変わる
    SceneNs::SceneData a, b;
    SceneNs::ObjectData boxA{}, boxB{};
    nlohmann::json entryA = SceneNs::MakeComponentEntry("BoxColliderComponent");
    SceneNs::SetField(entryA, "Half Extents", NS::Math::Vector3{1.0f, 1.0f, 1.0f});
    boxA.components.push_back(std::move(entryA));
    nlohmann::json entryB = SceneNs::MakeComponentEntry("BoxColliderComponent");
    SceneNs::SetField(entryB, "Half Extents", NS::Math::Vector3{2.0f, 1.0f, 1.0f});
    boxB.components.push_back(std::move(entryB));
    a.objects.push_back(std::move(boxA));
    b.objects.push_back(std::move(boxB));
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(SceneDataCrcTest, ObjectsSizeIsHashed)
{
    SceneNs::SceneData a, b;
    a.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    a.objects.push_back(LevelNs::MakeCellObject(1, 0, 0));
    b.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(SceneDataCrcTest, RotationIsHashed)
{
    SceneNs::SceneData a, b;
    a.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    SceneNs::ObjectData rotated = LevelNs::MakeCellObject(0, 0, 0);
    SceneNs::SetObjectRotation(rotated,
                               NS::Math::Quaternion::CreateFromYawPitchRoll(NS::Math::k_Pi * 0.5f, 0.0f, 0.0f));
    b.objects.push_back(rotated);
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(SceneDataCrcTest, ClassNameIsHashed)
{
    SceneNs::SceneData a, b;
    a.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    b.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    b.objects[0].className = "Player";
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

// 環境欄は見た目を確定する永続データなので、 差があれば dirty 検知の CRC も必ず動く
TEST(SceneDataCrcTest, EnvironmentIsHashed)
{
    SceneNs::SceneData c, d;
    c.environment.skyboxCubemapPath = "Assets/Skybox/a/";
    d.environment.skyboxCubemapPath = "Assets/Skybox/b/";
    EXPECT_NE(c.ComputeCrc32(), d.ComputeCrc32());
}

TEST(SceneDataCrcTest, VectorCapacityDoesNotAffectCrc)
{
    SceneNs::SceneData a, b;
    a.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    b.objects.reserve(1000);
    b.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    EXPECT_EQ(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(SceneDataComponents, ComponentEntryHoldsTypeAndFields)
{
    nlohmann::json entry = SceneNs::MakeComponentEntry("BoxColliderComponent");
    SceneNs::SetField(entry, "Half Extents", NS::Math::Vector3{0.5f, 0.5f, 0.5f});
    SceneNs::SetField(entry, "Radius", 1.0f);

    EXPECT_EQ(SceneNs::ComponentEntryType(entry), "BoxColliderComponent");
    ASSERT_EQ(entry.at("fields").size(), 2u);
    EXPECT_TRUE(SceneNs::HasField(entry, "Half Extents"));
    EXPECT_TRUE(SceneNs::HasField(entry, "Radius"));
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

TEST(SceneDataComponents, Crc32ChangesWhenComponentAdded)
{
    SceneNs::SceneData a, b;
    a.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));

    SceneNs::ObjectData withComponent = LevelNs::MakeCellObject(0, 0, 0);
    withComponent.components.push_back(SceneNs::MakeComponentEntry("HazardComponent"));
    b.objects.push_back(withComponent);

    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}
