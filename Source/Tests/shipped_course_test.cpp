#include "Editor/LevelFilePaths.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Scene/SceneData.h"
#include "Runtime/Object/Scene/SceneJson.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <set>
#include <string_view>
#include <vector>

namespace SceneNs = NS::Object;
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
    bool LoadShippedCourse(SceneNs::SceneData& outScene)
    {
        const auto path = EditorNs::BuildLevelPath("new_scene");
        if (!path.has_value())
            return false;
        return SceneNs::LoadSceneFromJsonFile(outScene, *path);
    }

    std::vector<float> CollectBreakableField(const SceneNs::SceneData& scene, std::string_view fieldName)
    {
        std::vector<float> values;
        for (const SceneNs::ObjectData& object : scene.objects)
        {
            const nlohmann::json* entry = SceneNs::FindComponentEntry(object, "BreakableComponent");
            if (entry == nullptr)
                continue;
            values.push_back(SceneNs::FieldFloat(*entry, fieldName, -1.0f));
        }
        return values;
    }
} // namespace

// 質量が全部同じだと飛距離の違いが出ず、重さが飛距離に現れているかをこのコースで確かめられない
TEST(ShippedCourse, MassesHaveAtLeastTwoDistinctValues)
{
    SceneNs::SceneData scene;
    ASSERT_TRUE(LoadShippedCourse(scene));
    const std::vector<float> masses = CollectBreakableField(scene, "質量");
    ASSERT_FALSE(masses.empty());
    const std::set<float> distinct(masses.begin(), masses.end());
    EXPECT_GE(distinct.size(), 2u);
}

// 破壊を許可した時、耐久の最大が威力の上限以下だと溜め切りで全部壊せ、跳ね返される壁が無くなる
TEST(ShippedCourse, ToughnessHasUnbreakableWall)
{
    SceneNs::SceneData scene;
    ASSERT_TRUE(LoadShippedCourse(scene));
    const std::vector<float> toughness = CollectBreakableField(scene, "耐久");
    ASSERT_FALSE(toughness.empty());
    const float maxToughness = *std::max_element(toughness.begin(), toughness.end());
    EXPECT_GT(maxToughness, k_MaxImpactPower);
}

// 耐久が素当ての威力を超える物しか無いと、溜めを挟まないと 1 つも壊せない
TEST(ShippedCourse, ToughnessHasBreakableTarget)
{
    SceneNs::SceneData scene;
    ASSERT_TRUE(LoadShippedCourse(scene));
    const std::vector<float> toughness = CollectBreakableField(scene, "耐久");
    ASSERT_FALSE(toughness.empty());
    const float minToughness = *std::min_element(toughness.begin(), toughness.end());
    EXPECT_LE(minToughness, k_PlainHitPower);
}
