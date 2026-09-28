#include <Game/Level/ColliderBounds.h>
#include <Game/Level/HitTier.h>
#include <Game/Level/HitZones.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Core/OBB.h>
#include <Runtime/Object/Component.h>
#include <Runtime/Object/Components/BoxCollider.h>
#include <Runtime/Object/Components/SphereCollider.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/ReflectionJson.h>
#include <Runtime/Object/Reflection/TypeRegistry.h>
#include <Runtime/Object/Transform.h>

#include <cmath>
#include <gtest/gtest.h>
#include <limits>

namespace
{
    using NS::Core::Vector3;
    using NS::Game::Level::HitTier;
    using NS::Game::Level::HitZoneJudgement;
    using NS::Game::Level::HitZones;
    using NS::Game::Level::ZoneRings;
    using NS::Obj::GameObject;

    // 同梱の場面の自機の半径
    constexpr float k_PlayerRadius = 0.65f;
    constexpr float k_Tolerance = 1.0e-4f;

    // 同梱の場面の相手と同じ、半径 0.5 m の玉
    struct BallTarget
    {
        GameObject object;
        NS::Obj::SphereCollider* sphere = nullptr;
        HitZones* zones = nullptr;

        explicit BallTarget(const Vector3& position)
        {
            object.Root().SetPosition(position);
            sphere = object.AddComponent<NS::Obj::SphereCollider>();
            sphere->SetRadius(0.5f);
            zones = object.AddComponent<HitZones>();
        }
    };

    // 原点の相手の横を +x へ通る線。lateral は線と相手の中心の z の差
    HitZoneJudgement JudgeAlongX(const HitZones& zones, float lateral)
    {
        HitZoneJudgement result;
        EXPECT_TRUE(zones.Judge(Vector3{-3.0f, 0.0f, lateral}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, result));
        return result;
    }
} // namespace

// 足した直後は 3 段で真ん中 0.5 m・惜しい 1.0 m。縁ちょうどは外側の段
TEST(HitZonesTest, RangesSplitTheTiersAndTheEdgeFallsOutside)
{
    BallTarget target{Vector3{0.0f, 0.0f, 0.0f}};

    EXPECT_EQ(JudgeAlongX(*target.zones, 0.2f).tier, HitTier::Center);
    EXPECT_EQ(JudgeAlongX(*target.zones, 0.5f).tier, HitTier::Near);
    EXPECT_EQ(JudgeAlongX(*target.zones, 0.7f).tier, HitTier::Near);
    EXPECT_EQ(JudgeAlongX(*target.zones, 1.0f).tier, HitTier::Wide);
    EXPECT_EQ(JudgeAlongX(*target.zones, 1.5f).tier, HitTier::Wide);
}

// 球の相手はどの向きから当てても、同じ横ずれなら同じ段と同じ威力の入力になる
TEST(HitZonesTest, BallGivesTheSameTierFromEightDirections)
{
    BallTarget target{Vector3{2.0f, 0.0f, -1.0f}};
    const Vector3 center{2.0f, 0.0f, -1.0f};
    const float lateral = 0.7f;
    const float expected = lateral / (0.5f + k_PlayerRadius);

    for (int i = 0; i < 8; ++i)
    {
        SCOPED_TRACE(i);
        const float angle = static_cast<float>(i) * 0.25f * 3.14159265f;
        const Vector3 direction{std::cos(angle), 0.0f, std::sin(angle)};
        const Vector3 side{-std::sin(angle), 0.0f, std::cos(angle)};
        HitZoneJudgement result;
        ASSERT_TRUE(target.zones->Judge(center - direction * 3.0f + side * lateral, direction, k_PlayerRadius, result));
        EXPECT_EQ(result.tier, HitTier::Near);
        EXPECT_NEAR(result.offset01, expected, k_Tolerance);
    }
}

// 威力の入力は中心を通る線で 0、かすめる線で 1 に近い。届かない線は比が 1 を超え、横ずれは 1 で止まる
TEST(HitZonesTest, OffsetIsZeroThroughTheCenterAndOneAtTheGrazingEdge)
{
    BallTarget target{Vector3{0.0f, 0.0f, 0.0f}};
    const float reach = 0.5f + k_PlayerRadius;

    EXPECT_NEAR(JudgeAlongX(*target.zones, 0.0f).offset01, 0.0f, k_Tolerance);
    EXPECT_GT(JudgeAlongX(*target.zones, reach - 0.001f).offset01, 0.99f);

    const HitZoneJudgement missed = JudgeAlongX(*target.zones, 2.0f);
    EXPECT_FLOAT_EQ(missed.offset01, 1.0f);
    EXPECT_NEAR(missed.ratio, 2.0f / reach, k_Tolerance);
}

// 回した箱は向きの付いた箱を線に直交する軸へ投影した半幅で届く幅を出す
// 外接箱で測ると、細い板を長い辺に沿って通る線でも板の対角ぶん広く出る
TEST(HitZonesTest, RotatedBoxUsesItsOwnWidthAcrossTheLine)
{
    GameObject object;
    NS::Obj::BoxCollider* box = object.AddComponent<NS::Obj::BoxCollider>();
    box->SetHalfExtents(Vector3{1.0f, 0.5f, 0.2f});
    box->SetRotationEulerDegrees(Vector3{0.0f, 45.0f, 0.0f});
    HitZones* zones = object.AddComponent<HitZones>();
    const NS::Core::OBB obb = box->WorldOBB();

    // 板の長い辺に沿って進み、板の厚みの向きへ 0.5 m ずれた線
    const Vector3 direction = obb.axisX;
    const Vector3 side = obb.axisZ;
    HitZoneJudgement result;
    ASSERT_TRUE(zones->Judge(obb.center - direction * 3.0f + side * 0.5f, direction, k_PlayerRadius, result));

    EXPECT_NEAR(result.ratio, 0.5f / (0.2f + k_PlayerRadius), k_Tolerance);
}

// 高さは見ない。上から見て同じ線なら、高さが違っても同じ段と同じ横ずれ
TEST(HitZonesTest, HeightDoesNotChangeTheJudgement)
{
    BallTarget target{Vector3{0.0f, 0.0f, 0.0f}};
    HitZoneJudgement level;
    HitZoneJudgement above;
    ASSERT_TRUE(target.zones->Judge(Vector3{-3.0f, 0.0f, 0.7f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, level));
    ASSERT_TRUE(target.zones->Judge(Vector3{-3.0f, 2.0f, 0.7f}, Vector3{1.0f, -0.5f, 0.0f}, k_PlayerRadius, above));

    EXPECT_EQ(above.tier, level.tier);
    EXPECT_NEAR(above.offset01, level.offset01, k_Tolerance);
}

// 段の数 2 は真ん中と外れ。惜しいの範囲の値は残り、3 に戻すと元の段に戻る
TEST(HitZonesTest, TwoTiersTurnNearIntoWideAndKeepTheNearRange)
{
    BallTarget target{Vector3{0.0f, 0.0f, 0.0f}};
    ASSERT_EQ(JudgeAlongX(*target.zones, 0.7f).tier, HitTier::Near);

    target.zones->SetTierCount(2);
    EXPECT_EQ(JudgeAlongX(*target.zones, 0.7f).tier, HitTier::Wide);
    EXPECT_EQ(JudgeAlongX(*target.zones, 0.2f).tier, HitTier::Center);
    EXPECT_FLOAT_EQ(target.zones->NearRadius(), 1.0f);

    target.zones->SetTierCount(3);
    EXPECT_EQ(JudgeAlongX(*target.zones, 0.7f).tier, HitTier::Near);
}

// 相手を 2 倍に拡縮すると範囲も 2 倍になる
TEST(HitZonesTest, ScalingTheTargetScalesTheRanges)
{
    BallTarget target{Vector3{0.0f, 0.0f, 0.0f}};
    target.object.Root().SetScale(Vector3{2.0f, 2.0f, 2.0f});

    EXPECT_EQ(JudgeAlongX(*target.zones, 0.9f).tier, HitTier::Center);
    EXPECT_EQ(JudgeAlongX(*target.zones, 1.9f).tier, HitTier::Near);
    EXPECT_EQ(JudgeAlongX(*target.zones, 2.1f).tier, HitTier::Wide);
}

// 欄は段の数を 2〜3、範囲の負と非数を 0 へ丸める。真ん中が惜しい以上なら崩れとして返す
TEST(HitZonesTest, FieldsAreClampedAndOrderBreaksAreReported)
{
    HitZones zones;
    EXPECT_EQ(zones.TierCount(), 3);
    EXPECT_FLOAT_EQ(zones.CenterRadius(), 0.5f);
    EXPECT_FLOAT_EQ(zones.NearRadius(), 1.0f);
    EXPECT_FALSE(zones.IsOrderBroken());

    zones.SetTierCount(5);
    EXPECT_EQ(zones.TierCount(), 3);
    zones.SetTierCount(1);
    EXPECT_EQ(zones.TierCount(), 2);

    zones.SetCenterRadius(-1.0f);
    EXPECT_FLOAT_EQ(zones.CenterRadius(), 0.0f);
    zones.SetNearRadius(std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(zones.NearRadius(), 0.0f);

    zones.SetTierCount(3);
    zones.SetCenterRadius(1.2f);
    zones.SetNearRadius(1.0f);
    EXPECT_TRUE(zones.IsOrderBroken());
    // 2 段では惜しいを見ないので崩れにならない
    zones.SetTierCount(2);
    EXPECT_FALSE(zones.IsOrderBroken());
}

// 線の通った点は、相手の中心を線へ垂直に落とした点を中心の高さに置いた物
TEST(HitZonesTest, LinePointIsTheCenterDroppedOntoTheLineAtTheCenterHeight)
{
    BallTarget target{Vector3{0.0f, 1.0f, 0.0f}};
    HitZoneJudgement result;
    ASSERT_TRUE(target.zones->Judge(Vector3{-3.0f, 0.2f, 0.7f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, result));

    EXPECT_NEAR(result.linePoint.x, 0.0f, k_Tolerance);
    EXPECT_NEAR(result.linePoint.y, 1.0f, k_Tolerance);
    EXPECT_NEAR(result.linePoint.z, 0.7f, k_Tolerance);
    EXPECT_NEAR(result.along, 3.0f, k_Tolerance);
}

// 水平の向きが無い線は判定しない
TEST(HitZonesTest, VerticalDirectionIsNotJudged)
{
    BallTarget target{Vector3{0.0f, 0.0f, 0.0f}};
    HitZoneJudgement result;
    EXPECT_FALSE(target.zones->Judge(Vector3{0.0f, 3.0f, 0.0f}, Vector3{0.0f, -1.0f, 0.0f}, k_PlayerRadius, result));
}

// 体はぶつかる当たり判定 1 つ。トリガーは体にならず、ぶつかる当たり判定が 2 つあれば判定しない
TEST(HitZonesTest, OnlyOneCollidingColliderIsTheBody)
{
    {
        SCOPED_TRACE("当たり判定なし");
        GameObject object;
        HitZones* zones = object.AddComponent<HitZones>();
        HitZoneJudgement result;
        EXPECT_FALSE(zones->Judge(Vector3{-3.0f, 0.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, result));
        EXPECT_EQ(NS::Game::Level::FindBodyCollider(object).body, nullptr);
    }
    {
        SCOPED_TRACE("トリガーの箱だけ");
        GameObject object;
        object.AddComponent<NS::Obj::BoxCollider>()->SetTrigger(true);
        HitZones* zones = object.AddComponent<HitZones>();
        HitZoneJudgement result;
        EXPECT_FALSE(zones->Judge(Vector3{-3.0f, 0.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, result));
    }
    {
        SCOPED_TRACE("トリガーの箱とぶつかる球");
        GameObject object;
        object.AddComponent<NS::Obj::BoxCollider>()->SetTrigger(true);
        NS::Obj::SphereCollider* sphere = object.AddComponent<NS::Obj::SphereCollider>();
        HitZones* zones = object.AddComponent<HitZones>();
        const NS::Game::Level::BodyColliderSearch search = NS::Game::Level::FindBodyCollider(object);
        EXPECT_EQ(search.body, sphere);
        EXPECT_EQ(search.count, 1);
        HitZoneJudgement result;
        EXPECT_TRUE(zones->Judge(Vector3{-3.0f, 0.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, result));
    }
    {
        SCOPED_TRACE("ぶつかる箱と球");
        GameObject object;
        object.AddComponent<NS::Obj::BoxCollider>();
        object.AddComponent<NS::Obj::SphereCollider>();
        HitZones* zones = object.AddComponent<HitZones>();
        const NS::Game::Level::BodyColliderSearch search = NS::Game::Level::FindBodyCollider(object);
        EXPECT_EQ(search.body, nullptr);
        EXPECT_EQ(search.count, 2);
        HitZoneJudgement result;
        EXPECT_FALSE(zones->Judge(Vector3{-3.0f, 0.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, result));
    }
}

// エディタが描く円は体の中心まわりで、拡縮込みの半径。2 段の間は惜しいの円を描かない
TEST(HitZonesTest, RingsFollowTheBodyAndTheScale)
{
    BallTarget target{Vector3{1.0f, 2.0f, 3.0f}};
    target.object.Root().SetScale(Vector3{2.0f, 2.0f, 2.0f});
    ZoneRings rings;
    ASSERT_TRUE(target.zones->TryGetRings(rings));
    EXPECT_NEAR(rings.center.x, 1.0f, k_Tolerance);
    EXPECT_NEAR(rings.center.y, 2.0f, k_Tolerance);
    EXPECT_NEAR(rings.center.z, 3.0f, k_Tolerance);
    EXPECT_NEAR(rings.centerRadius, 1.0f, k_Tolerance);
    EXPECT_NEAR(rings.nearRadius, 2.0f, k_Tolerance);
    EXPECT_FALSE(rings.orderBroken);

    target.zones->SetTierCount(2);
    ASSERT_TRUE(target.zones->TryGetRings(rings));
    EXPECT_FLOAT_EQ(rings.nearRadius, 0.0f);
}

// 段の数と 2 つの範囲は保存と再読込で残る
TEST(HitZonesTest, FieldsSurviveSaveAndLoad)
{
    GameObject source;
    HitZones* zones = source.AddComponent<HitZones>();
    zones->SetTierCount(2);
    zones->SetCenterRadius(0.3f);
    zones->SetNearRadius(0.8f);
    const nlohmann::json saved = NS::Obj::SerializeComponent(*zones);
    EXPECT_EQ(saved["type"], "HitZones");

    GameObject destination;
    NS::Obj::Component* created = NS::Obj::CreateComponent("HitZones", destination);
    ASSERT_NE(created, nullptr);
    NS::Obj::ApplyJsonFields(*created, saved["fields"]);
    const HitZones* restored = destination.FindComponent<HitZones>();
    ASSERT_NE(restored, nullptr);

    EXPECT_EQ(restored->TierCount(), 2);
    EXPECT_FLOAT_EQ(restored->CenterRadius(), 0.3f);
    EXPECT_FLOAT_EQ(restored->NearRadius(), 0.8f);
}
