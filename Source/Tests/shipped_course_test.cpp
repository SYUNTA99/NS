#include "Editor/LevelFilePaths.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Scene/SceneJson.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <set>
#include <string_view>
#include <vector>

namespace SceneNs = NS::Obj;
namespace EditorNs = NS::Editor;

namespace
{
    // チャージ倍率カーブの右端。溜め切った 1 回で威力が倍になる
    constexpr float k_MaxChargeScale = 2.0f;

    // 威力は 溜め倍率 × 突進位置係数 で、位置係数は 1.0 を超えない
    // 破壊を許可した時、耐久が威力の上限を超える物だけが必ず跳ね返す
    constexpr float k_MaxImpactPower = k_MaxChargeScale;

    // 溜めずに当てた時の威力
    constexpr float k_PlainHitPower = 1.0f;

    // 検査対象は Assets の実ファイルそのもの。固定データの複製を検査すると、実ファイル側の壊れを見逃す
    bool LoadShippedCourse(nlohmann::json& outScene)
    {
        const std::optional<std::string> path = EditorNs::BuildLevelPath("new_scene");
        if (!path.has_value())
            return false;
        return SceneNs::LoadSceneFromJsonFile(outScene, *path);
    }

    // 壊せる物ごとに、同じ配置物の typeName の欄を集める。typeName を持たない物は -1 を積む
    std::vector<float> CollectBreakableField(const nlohmann::json& scene,
                                             std::string_view typeName,
                                             std::string_view fieldName)
    {
        std::vector<float> values;
        for (const nlohmann::json& object : SceneNs::SceneJsonObjects(scene))
        {
            if (SceneNs::FindComponentEntry(object, "Breakable") == nullptr)
                continue;
            const nlohmann::json* entry = SceneNs::FindComponentEntry(object, typeName);
            if (entry == nullptr)
            {
                values.push_back(-1.0f);
                continue;
            }
            values.push_back(SceneNs::FieldFloat(*entry, fieldName, -1.0f));
        }
        return values;
    }
} // namespace

// 壊せる物の重さと面は RigidBody が持つ。無いと押し飛ばした時に既定の重さ 1 で飛び、置いた値が効かない
// 置かれている間は動かないよう、キネマティックで置く
TEST(ShippedCourse, BreakablesCarryKinematicRigidBody)
{
    nlohmann::json scene = SceneNs::MakeSceneJson();
    ASSERT_TRUE(LoadShippedCourse(scene));
    int breakables = 0;
    for (const nlohmann::json& object : SceneNs::SceneJsonObjects(scene))
    {
        if (SceneNs::FindComponentEntry(object, "Breakable") == nullptr)
            continue;
        ++breakables;
        const nlohmann::json* body = SceneNs::FindComponentEntry(object, "RigidBody");
        ASSERT_NE(body, nullptr);
        const nlohmann::json* fields = SceneNs::ComponentEntryFields(*body);
        ASSERT_NE(fields, nullptr);
        ASSERT_TRUE(fields->contains("キネマティック"));
        EXPECT_TRUE(fields->at("キネマティック").get<bool>());
    }
    EXPECT_GT(breakables, 0);
}

// 質量が全部同じだと飛距離の違いが出ず、重さが飛距離に現れているかをこのコースで確かめられない
TEST(ShippedCourse, MassesHaveAtLeastTwoDistinctValues)
{
    nlohmann::json scene = SceneNs::MakeSceneJson();
    ASSERT_TRUE(LoadShippedCourse(scene));
    const std::vector<float> masses = CollectBreakableField(scene, "RigidBody", "質量");
    ASSERT_FALSE(masses.empty());
    const std::set<float> distinct(masses.begin(), masses.end());
    EXPECT_GE(distinct.size(), 2u);
}

// 破壊を許可した時、耐久の最大が威力の上限以下だと溜め切りで全部壊せ、跳ね返される壁が無くなる
TEST(ShippedCourse, ToughnessHasUnbreakableWall)
{
    nlohmann::json scene = SceneNs::MakeSceneJson();
    ASSERT_TRUE(LoadShippedCourse(scene));
    const std::vector<float> toughness = CollectBreakableField(scene, "Breakable", "耐久");
    ASSERT_FALSE(toughness.empty());
    const float maxToughness = *std::max_element(toughness.begin(), toughness.end());
    EXPECT_GT(maxToughness, k_MaxImpactPower);
}

// 耐久が素当ての威力を超える物しか無いと、溜めを挟まないと 1 つも壊せない
TEST(ShippedCourse, ToughnessHasBreakableTarget)
{
    nlohmann::json scene = SceneNs::MakeSceneJson();
    ASSERT_TRUE(LoadShippedCourse(scene));
    const std::vector<float> toughness = CollectBreakableField(scene, "Breakable", "耐久");
    ASSERT_FALSE(toughness.empty());
    const float minToughness = *std::min_element(toughness.begin(), toughness.end());
    EXPECT_LE(minToughness, k_PlainHitPower);
}
