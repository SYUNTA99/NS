#include "Editor/LevelFilePaths.h"
#include "Game/Level/KillZone.h"
#include "Game/Player.h"
#include "Game/Player/ChargeEffects.h"
#include "Game/Player/ImpactEffects.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Scene/SceneJson.h"

#include <algorithm>
#include <array>
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

    // 威力は 溜め倍率 × 当たった色の威力の倍率 で、同梱の場面の倍率は 1.0 を超えない (EveryBreakableCarriesHitZones)
    // 破壊を許可した時、耐久が威力の上限を超える物だけが必ず跳ね返す
    constexpr float k_MaxImpactPower = k_MaxChargeScale;

    // 溜めずに当てた時の威力
    constexpr float k_PlainHitPower = 1.0f;

    // 置ける質量の下限。これより軽い物では、当てた自機の返りが 0 に近づく
    constexpr float k_MinPlacedMass = 0.5f;

    // 当てられる物を置いた同梱の場面。開始シーンと、進むのに当てる事が要るコース
    constexpr std::array<const char*, 2> k_TargetSceneNames{"new_scene", "course"};

    // 検査対象は Assets の実ファイルそのもの。固定データの複製を検査すると、実ファイル側の壊れを見逃す
    bool LoadShippedCourse(nlohmann::json& outScene, std::string_view name)
    {
        const std::optional<std::string> path = EditorNs::BuildLevelPath(name);
        if (!path.has_value())
        {
            return false;
        }
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
    for (const char* name : k_TargetSceneNames)
    {
        SCOPED_TRACE(name);
        nlohmann::json scene = SceneNs::MakeSceneJson();
        if (!LoadShippedCourse(scene, name))
        {
            ADD_FAILURE() << name << " の場面が読めない";
            continue;
        }
        int breakables = 0;
        for (const nlohmann::json& object : SceneNs::SceneJsonObjects(scene))
        {
            if (SceneNs::FindComponentEntry(object, "Breakable") == nullptr)
            {
                continue;
            }
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
}

// 質量が全部同じだと飛距離の違いが出ず、重さが飛距離に現れているかをこの場面で確かめられない
TEST(ShippedCourse, MassesHaveAtLeastTwoDistinctValues)
{
    for (const char* name : k_TargetSceneNames)
    {
        SCOPED_TRACE(name);
        nlohmann::json scene = SceneNs::MakeSceneJson();
        if (!LoadShippedCourse(scene, name))
        {
            ADD_FAILURE() << name << " の場面が読めない";
            continue;
        }
        const std::vector<float> masses = CollectBreakableField(scene, "RigidBody", "質量");
        ASSERT_FALSE(masses.empty());
        const std::set<float> distinct(masses.begin(), masses.end());
        EXPECT_GE(distinct.size(), 2u);
    }
}

// 軽すぎる物に当てると自機がほとんど弾かれず、当てた手応えが返らない。エディタでは下限より軽い質量も置けるので、場面で縛る
TEST(ShippedCourse, PlacedMassesAreAtLeastHalf)
{
    for (const char* name : k_TargetSceneNames)
    {
        SCOPED_TRACE(name);
        nlohmann::json scene = SceneNs::MakeSceneJson();
        if (!LoadShippedCourse(scene, name))
        {
            ADD_FAILURE() << name << " の場面が読めない";
            continue;
        }
        const std::vector<float> masses = CollectBreakableField(scene, "RigidBody", "質量");
        ASSERT_FALSE(masses.empty());
        for (const float mass : masses)
        {
            EXPECT_GE(mass, k_MinPlacedMass) << name << " に質量 " << mass << " の当てられる物がある";
        }
    }
}

// Game.exe と Replay は落下死の体積を足さない。コースに無いと、床の外へ落ちた自機がやり直せずに落ち続ける
TEST(ShippedCourse, CourseCarriesAKillZone)
{
    nlohmann::json scene = SceneNs::MakeSceneJson();
    ASSERT_TRUE(LoadShippedCourse(scene, "course")) << "course の場面が読めない";
    int killZones = 0;
    for (const nlohmann::json& object : SceneNs::SceneJsonObjects(scene))
    {
        if (!NS::Game::Level::IsKillZoneObject(object))
        {
            continue;
        }
        ++killZones;
        // 当たり箱がトリガーでないと、落ちてきた自機が上面に立ち、重ならずに死ねない
        const nlohmann::json* box = SceneNs::FindComponentEntry(object, "BoxCollider");
        ASSERT_NE(box, nullptr);
        const nlohmann::json* fields = SceneNs::ComponentEntryFields(*box);
        ASSERT_NE(fields, nullptr);
        ASSERT_TRUE(fields->contains("トリガー"));
        EXPECT_TRUE(fields->at("トリガー").get<bool>());
    }
    EXPECT_EQ(killZones, 1);
}

// 段の面 (HitZones) を持たない壊せる物は体当たりの相手にならない
// 同梱の場面の相手は全部、残りの威力の倍率 0.7 と、前の「真ん中 0.5 m」を割合に直した気持ちいいの色 0.43 を持つ
// 色の威力の倍率は 1.0 を超えない。超えると溜めきりの威力が k_MaxImpactPower を超える
TEST(ShippedCourse, EveryBreakableCarriesHitZones)
{
    for (const char* name : k_TargetSceneNames)
    {
        SCOPED_TRACE(name);
        nlohmann::json scene = SceneNs::MakeSceneJson();
        ASSERT_TRUE(LoadShippedCourse(scene, name));
        const std::vector<float> remainders = CollectBreakableField(scene, "HitZones", "残りの威力の倍率");
        const std::vector<float> widths = CollectBreakableField(scene, "HitZoneArea", "横幅");
        const std::vector<float> heights = CollectBreakableField(scene, "HitZoneArea", "縦の幅");
        const std::vector<float> powers = CollectBreakableField(scene, "HitZoneArea", "威力の倍率");
        ASSERT_FALSE(remainders.empty());
        ASSERT_EQ(widths.size(), remainders.size());
        ASSERT_EQ(heights.size(), remainders.size());
        ASSERT_EQ(powers.size(), remainders.size());
        for (std::size_t i = 0; i < remainders.size(); ++i)
        {
            EXPECT_FLOAT_EQ(remainders[i], 0.7f) << i;
            EXPECT_FLOAT_EQ(widths[i], 0.43f) << i;
            EXPECT_FLOAT_EQ(heights[i], 0.43f) << i;
            EXPECT_GE(powers[i], 0.0f) << i;
            EXPECT_LE(powers[i], 1.0f) << i;
        }
    }
}

// 破壊を許可した時、耐久の最大が威力の上限以下だと溜め切りで全部壊せ、跳ね返される壁が無くなる
TEST(ShippedCourse, ToughnessHasUnbreakableWall)
{
    nlohmann::json scene = SceneNs::MakeSceneJson();
    ASSERT_TRUE(LoadShippedCourse(scene, "new_scene"));
    const std::vector<float> toughness = CollectBreakableField(scene, "Breakable", "耐久");
    ASSERT_FALSE(toughness.empty());
    const float maxToughness = *std::max_element(toughness.begin(), toughness.end());
    EXPECT_GT(maxToughness, k_MaxImpactPower);
}

// 耐久が素当ての威力を超える物しか無いと、溜めを挟まないと 1 つも壊せない
TEST(ShippedCourse, ToughnessHasBreakableTarget)
{
    nlohmann::json scene = SceneNs::MakeSceneJson();
    ASSERT_TRUE(LoadShippedCourse(scene, "new_scene"));
    const std::vector<float> toughness = CollectBreakableField(scene, "Breakable", "耐久");
    ASSERT_FALSE(toughness.empty());
    const float minToughness = *std::min_element(toughness.begin(), toughness.end());
    EXPECT_LE(minToughness, k_PlainHitPower);
}

// 自機のエフェクトの層は ChargeEffects と ImpactEffects が出す。場面に載っていないと、遊んでも Replay でも
// 層が 1 つも出ない
// 載せ方は場面の自機の component の項目なので、組み上げた自機から引けることまで見る
TEST(ShippedCourse, PlayerCarriesTheEffectComponents)
{
    for (const char* name : k_TargetSceneNames)
    {
        SCOPED_TRACE(name);
        nlohmann::json data = SceneNs::MakeSceneJson();
        if (!LoadShippedCourse(data, name))
        {
            ADD_FAILURE() << name << " の場面が読めない";
            continue;
        }
        SceneNs::Scene scene;
        scene.LoadJson(std::move(data));
        Player* player = FindPlayer(scene.Objects());
        ASSERT_NE(player, nullptr);
        EXPECT_NE(player->FindComponent<NS::Game::Player::ChargeEffects>(), nullptr);
        EXPECT_NE(player->FindComponent<NS::Game::Player::ImpactEffects>(), nullptr);
    }
}
