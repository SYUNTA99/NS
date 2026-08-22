#include "Editor/LevelFilePaths.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Scene/SceneData.h"
#include "Runtime/Object/Scene/SceneJson.h"

#include <algorithm>
#include <fstream>
#include <gtest/gtest.h>
#include <set>
#include <string>
#include <vector>

namespace SceneNs = NS::Object;
namespace EditorNs = NS::Editor;

namespace
{
    // 最高ダッシュ速度 16.0 ÷ 通常速度 8.0。走行で作れる勢いの比の上限で、耐久がこれを超える物は走って壊せない
    constexpr float k_MaxDashMomentumRatio = 2.0f;

    // 検査対象は Assets の実ファイルそのもの。固定データの複製を検査すると、実ファイル側の壊れを見逃す
    bool LoadCourse(SceneNs::SceneData& outScene)
    {
        const auto path = EditorNs::BuildLevelPath("collision_feel");
        if (!path.has_value())
            return false;
        return SceneNs::LoadSceneFromJsonFile(outScene, *path);
    }

    // BreakableComponent を持つ全配置物から欄の値を集める。質量と耐久の検証が共に使う
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

    const SceneNs::ObjectData* FindPlayer(const SceneNs::SceneData& scene)
    {
        for (const SceneNs::ObjectData& object : scene.objects)
        {
            if (object.className == "Player")
                return &object;
        }
        return nullptr;
    }
} // namespace

TEST(CollisionCourse, SceneFileLoadsWithObjects)
{
    SceneNs::SceneData scene;
    ASSERT_TRUE(LoadCourse(scene));
    EXPECT_GE(scene.objects.size(), 1u);
}

TEST(CollisionCourse, HasExactlySixBreakables)
{
    SceneNs::SceneData scene;
    ASSERT_TRUE(LoadCourse(scene));
    const std::vector<float> masses = CollectBreakableField(scene, "質量");
    EXPECT_EQ(masses.size(), 6u);
}

TEST(CollisionCourse, MassesHaveAtLeastTwoDistinctValues)
{
    // 質量が全部同じだと飛距離の違いが出ず、重さの表示をこのコースで確かめられない
    SceneNs::SceneData scene;
    ASSERT_TRUE(LoadCourse(scene));
    const std::vector<float> masses = CollectBreakableField(scene, "質量");
    const std::set<float> distinct(masses.begin(), masses.end());
    EXPECT_GE(distinct.size(), 2u);
}

TEST(CollisionCourse, ToughnessHasUnbreakableWall)
{
    // 耐久の最大が走行の比の上限 2.0 以下だと最高ダッシュで全部壊せてしまい、勢いを最大にしても跳ね返される壁が無くなる
    SceneNs::SceneData scene;
    ASSERT_TRUE(LoadCourse(scene));
    const std::vector<float> toughness = CollectBreakableField(scene, "耐久");
    ASSERT_FALSE(toughness.empty());
    const std::set<float> distinct(toughness.begin(), toughness.end());
    EXPECT_GE(distinct.size(), 2u);
    const float maxToughness = *std::max_element(toughness.begin(), toughness.end());
    EXPECT_GT(maxToughness, k_MaxDashMomentumRatio);
}

TEST(CollisionCourse, ToughnessHasBreakableTarget)
{
    // 比は当たった瞬間の実速度から作るので、最高ダッシュでも加速し切る前は 2.0 に届かない
    // 耐久 1.0 以下の物があれば、最高ダッシュの段のまま通常速度ぶんの当たりでも壊せる物が残る
    SceneNs::SceneData scene;
    ASSERT_TRUE(LoadCourse(scene));
    const std::vector<float> toughness = CollectBreakableField(scene, "耐久");
    ASSERT_FALSE(toughness.empty());
    const float minToughness = *std::min_element(toughness.begin(), toughness.end());
    EXPECT_LE(minToughness, 1.0f);
}

TEST(CollisionCourse, PlayerCarriesMomentumAndImpactResolver)
{
    // 勢いと衝突の裁定は自機側の Component の仕事。積み忘れると当たっても何も起きない場面になる
    SceneNs::SceneData scene;
    ASSERT_TRUE(LoadCourse(scene));
    const SceneNs::ObjectData* player = FindPlayer(scene);
    ASSERT_NE(player, nullptr);
    EXPECT_NE(SceneNs::FindComponentEntry(*player, "MomentumComponent"), nullptr);
    EXPECT_NE(SceneNs::FindComponentEntry(*player, "ImpactResolverComponent"), nullptr);
}

TEST(CollisionCourse, IdsAreUniqueInFile)
{
    // 読み込みは EnsureUniqueObjectIds が id の重複を黙って直してしまうので、
    // ファイルに書かれた素の id を直接見る。手で書いた場面の重複はここでしか捕まらない
    const auto path = EditorNs::BuildLevelPath("collision_feel");
    ASSERT_TRUE(path.has_value());
    std::ifstream file(*path);
    ASSERT_TRUE(file.is_open());
    const nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    ASSERT_FALSE(root.is_discarded());

    std::vector<std::uint32_t> ids;
    for (const nlohmann::json& object : root.at("objects"))
    {
        ids.push_back(object.at("id").get<std::uint32_t>());
        for (const nlohmann::json& component : object.at("components"))
            ids.push_back(component.at("id").get<std::uint32_t>());
    }
    const std::set<std::uint32_t> distinct(ids.begin(), ids.end());
    EXPECT_EQ(ids.size(), distinct.size());
    EXPECT_EQ(distinct.count(0u), 0u);
    // nextObjectId の更新漏れも読み込みの採番が直すので実行では現れず、ファイル側の検査でしか見えない
    EXPECT_GT(root.at("nextObjectId").get<std::uint32_t>(), *distinct.rbegin());
}
