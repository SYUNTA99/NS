#include <Game/Level/ColliderBounds.h>
#include <Game/Level/HitTier.h>
#include <Game/Level/HitZoneArea.h>
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
    using NS::Game::Level::HitZoneArea;
    using NS::Game::Level::HitZoneJudgement;
    using NS::Game::Level::HitZones;
    using NS::Obj::GameObject;

    // 同梱の場面の自機の半径
    constexpr float k_PlayerRadius = 0.65f;
    constexpr float k_Tolerance = 1.0e-4f;
    // 半径 0.5 m の玉へ半径 0.65 m の自機が当たる時の、面の左右と上下の半分の幅
    constexpr float k_BallReach = 0.5f + k_PlayerRadius;

    // 同梱の場面の相手と同じ、半径 0.5 m の玉。色はまだ無い
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

    // 原点の相手へ -x から +x へ進む線。lateral は線と相手の中心の z の差
    HitZoneJudgement JudgeAlongX(const HitZones& zones, float lateral)
    {
        HitZoneJudgement result;
        EXPECT_TRUE(zones.Judge(Vector3{-3.0f, 0.0f, lateral}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, result));
        return result;
    }
} // namespace

// 面の上の位置は、相手の中心から線までの左右と上下のずれを「半幅 + 自機の半径」「半分の高さ + 自機の半径」で割った物
// 自機から見て右が正。+x へ進む時の右は -z
TEST(HitZonesTest, FacePositionIsTheOffsetOverTheReach)
{
    BallTarget target{Vector3{0.0f, 1.0f, 0.0f}};
    HitZoneJudgement result;
    ASSERT_TRUE(target.zones->Judge(Vector3{-3.0f, 1.3f, -0.4f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, result));
    EXPECT_NEAR(result.u, 0.4f / k_BallReach, k_Tolerance);
    EXPECT_NEAR(result.v, 0.3f / k_BallReach, k_Tolerance);
}

// 面はいつも自機が来る向きを向く。どの向きから当てても、同じ右と上のずれなら面の上の同じ位置
TEST(HitZonesTest, FaceTurnsTowardTheIncomingDirection)
{
    BallTarget target{Vector3{2.0f, 0.0f, -1.0f}};
    const Vector3 center{2.0f, 0.0f, -1.0f};
    for (int i = 0; i < 8; ++i)
    {
        SCOPED_TRACE(i);
        const float angle = static_cast<float>(i) * 0.25f * 3.14159265f;
        const Vector3 direction{std::cos(angle), 0.0f, std::sin(angle)};
        // 左手系で上から見た、進む向きの右
        const Vector3 right{direction.z, 0.0f, -direction.x};
        HitZoneJudgement result;
        ASSERT_TRUE(target.zones->Judge(
            center - direction * 3.0f + right * 0.3f + Vector3{0.0f, -0.2f, 0.0f}, direction, k_PlayerRadius, result));
        EXPECT_NEAR(result.u, 0.3f / k_BallReach, k_Tolerance);
        EXPECT_NEAR(result.v, -0.2f / k_BallReach, k_Tolerance);
    }
}

// 同じ割合のずれは、相手の大きさが違っても面の上の同じ位置に入る
TEST(HitZonesTest, SameShareLandsOnTheSamePlaceOfAnySizedTarget)
{
    BallTarget smallBall{Vector3{0.0f, 0.0f, 0.0f}};
    BallTarget largeBall{Vector3{0.0f, 0.0f, 0.0f}};
    largeBall.object.Root().SetScale(Vector3{3.0f, 3.0f, 3.0f});
    const float largeReach = 1.5f + k_PlayerRadius;

    const HitZoneJudgement onSmall = JudgeAlongX(*smallBall.zones, -0.5f * k_BallReach);
    const HitZoneJudgement onLarge = JudgeAlongX(*largeBall.zones, -0.5f * largeReach);
    EXPECT_NEAR(onSmall.u, 0.5f, k_Tolerance);
    EXPECT_NEAR(onLarge.u, 0.5f, k_Tolerance);
}

// 箱の半分の高さは、向きの付いた箱の 3 軸を縦へ写した長さの和
TEST(HitZonesTest, BoxHalfHeightComesFromItsAxes)
{
    GameObject object;
    NS::Obj::BoxCollider* box = object.AddComponent<NS::Obj::BoxCollider>();
    box->SetHalfExtents(Vector3{1.0f, 0.5f, 0.2f});
    HitZones* zones = object.AddComponent<HitZones>();

    HitZoneJudgement result;
    ASSERT_TRUE(zones->Judge(Vector3{-3.0f, 0.5f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, result));
    EXPECT_NEAR(result.v, 0.5f / (0.5f + k_PlayerRadius), k_Tolerance);
}

// 色が 1 つも無ければ、どこも外れで残りの威力の倍率
TEST(HitZonesTest, NoAreaMeansEverythingIsWide)
{
    BallTarget target{Vector3{0.0f, 0.0f, 0.0f}};
    const HitZoneJudgement result = JudgeAlongX(*target.zones, 0.0f);
    EXPECT_EQ(result.tier, HitTier::Wide);
    EXPECT_FLOAT_EQ(result.powerScale, 0.7f);
}

// 気持ちいいの色に入れば中心近くで、その色の威力の倍率。外は外れで残りの倍率
TEST(HitZonesTest, CenterAreaGivesTheCenterTierAndItsPower)
{
    BallTarget target{Vector3{0.0f, 0.0f, 0.0f}};
    HitZoneArea* area = target.object.AddComponent<HitZoneArea>();
    area->SetPowerScale(1.2f);

    const HitZoneJudgement inside = JudgeAlongX(*target.zones, 0.4f);
    EXPECT_EQ(inside.tier, HitTier::Center);
    EXPECT_FLOAT_EQ(inside.powerScale, 1.2f);

    // 0.5 ÷ 1.15 ≒ 0.435 は 0.43 の外
    const HitZoneJudgement outside = JudgeAlongX(*target.zones, 0.5f);
    EXPECT_EQ(outside.tier, HitTier::Wide);
    EXPECT_FLOAT_EQ(outside.powerScale, 0.7f);
}

// 気持ちいいと外れの色が重なる所は気持ちいい。外れの色の中は外れで、その色の威力の倍率
TEST(HitZonesTest, CenterWinsWhereAreasOverlap)
{
    BallTarget target{Vector3{0.0f, 0.0f, 0.0f}};
    HitZoneArea* wide = target.object.AddComponent<HitZoneArea>();
    wide->SetCenter(false);
    wide->SetWidth(1.0f);
    wide->SetHeight(1.0f);
    wide->SetPowerScale(0.9f);
    target.object.AddComponent<HitZoneArea>();

    const HitZoneJudgement center = JudgeAlongX(*target.zones, 0.0f);
    EXPECT_EQ(center.tier, HitTier::Center);
    EXPECT_FLOAT_EQ(center.powerScale, 1.0f);

    const HitZoneJudgement outer = JudgeAlongX(*target.zones, 0.8f);
    EXPECT_EQ(outer.tier, HitTier::Wide);
    EXPECT_FLOAT_EQ(outer.powerScale, 0.9f);
}

// 色は上下も見る。上へずらした赤は、同じ高さの線では外れ
TEST(HitZonesTest, AreasAlsoSplitUpAndDown)
{
    BallTarget target{Vector3{0.0f, 0.0f, 0.0f}};
    HitZoneArea* area = target.object.AddComponent<HitZoneArea>();
    area->SetHeight(0.2f);
    area->SetCenterV(0.5f);

    EXPECT_EQ(JudgeAlongX(*target.zones, 0.0f).tier, HitTier::Wide);
    HitZoneJudgement high;
    ASSERT_TRUE(
        target.zones->Judge(Vector3{-3.0f, 0.5f * k_BallReach, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, high));
    EXPECT_EQ(high.tier, HitTier::Center);
}

// 残りの威力の倍率は負を 0 にし、非数と無限は 0。保存と再読込で残る
TEST(HitZonesTest, RemainderPowerIsClampedAndSaved)
{
    HitZones zones;
    EXPECT_FLOAT_EQ(zones.RemainderPowerScale(), 0.7f);
    zones.SetRemainderPowerScale(-1.0f);
    EXPECT_FLOAT_EQ(zones.RemainderPowerScale(), 0.0f);
    zones.SetRemainderPowerScale(std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(zones.RemainderPowerScale(), 0.0f);

    GameObject source;
    HitZones* saved = source.AddComponent<HitZones>();
    saved->SetRemainderPowerScale(0.55f);
    const nlohmann::json json = NS::Obj::SerializeComponent(*saved);
    GameObject destination;
    NS::Obj::Component* created = NS::Obj::CreateComponent("HitZones", destination);
    ASSERT_NE(created, nullptr);
    NS::Obj::ApplyJsonFields(*created, json["fields"]);
    const HitZones* restored = destination.FindComponent<HitZones>();
    ASSERT_NE(restored, nullptr);
    EXPECT_FLOAT_EQ(restored->RemainderPowerScale(), 0.55f);
}
// 線の起点がもう相手に触れる所まで来ていても、線が触れる球に入る所で決める
// 予測は遠くから、当たりは触れてから Judge を呼ぶ。今の向きで決めると、同じ線でも予測と当たりの段が割れる
TEST(HitZonesTest, BallAlreadyTouchingIsJudgedWhereTheLineEntered)
{
    BallTarget target{Vector3{0.0f, 0.0f, 0.0f}};
    const auto judgeFrom = [&](const Vector3& origin) {
        HitZoneJudgement result;
        EXPECT_TRUE(target.zones->Judge(origin, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, result));
        return result;
    };

    // 横ずれ 0.5 m の線が表面に触れる点を、遠くからと、もう触れる所まで来てからの 2 回で出す
    const HitZoneJudgement distant = judgeFrom(Vector3{-3.0f, 0.0f, 0.5f});
    const HitZoneJudgement touching = judgeFrom(Vector3{-0.9f, 0.0f, 0.5f});
    EXPECT_EQ(touching.tier, distant.tier);
    EXPECT_NEAR(touching.u, distant.u, k_Tolerance);
    EXPECT_NEAR(touching.surfacePoint.x, distant.surfacePoint.x, k_Tolerance);
    EXPECT_NEAR(touching.surfacePoint.z, distant.surfacePoint.z, k_Tolerance);
}

// 触れる点は相手の表面の上。届かない線は、線に一番近い表面の点
TEST(HitZonesTest, SurfacePointIsWhereTheBallTouches)
{
    BallTarget target{Vector3{0.0f, 1.0f, 0.0f}};
    HitZoneJudgement result;
    ASSERT_TRUE(target.zones->Judge(Vector3{-3.0f, 1.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, result));
    EXPECT_NEAR(result.surfacePoint.x, -0.5f, k_Tolerance);
    EXPECT_NEAR(result.surfacePoint.y, 1.0f, k_Tolerance);
    EXPECT_NEAR(result.surfacePoint.z, 0.0f, k_Tolerance);

    ASSERT_TRUE(target.zones->Judge(Vector3{-3.0f, 1.0f, 2.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, result));
    EXPECT_EQ(result.tier, HitTier::Wide);
    EXPECT_NEAR(result.surfacePoint.x, 0.0f, k_Tolerance);
    EXPECT_NEAR(result.surfacePoint.y, 1.0f, k_Tolerance);
    EXPECT_NEAR(result.surfacePoint.z, 0.5f, k_Tolerance);
}

// 球の相手はどの向きから当てても、同じ横ずれなら同じ威力の入力になる
TEST(HitZonesTest, BallGivesTheSameOffsetFromEightDirections)
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
