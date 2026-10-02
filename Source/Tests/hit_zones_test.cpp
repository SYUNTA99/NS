#include "Game/Level/HitTier.h"
#include "Game/Level/HitZones.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Player.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Core/OBB.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/ObjectJson.h"
#include "Runtime/Object/Reflection/Archetype.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/ReflectionJson.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Scene/SceneJson.h"
#include "Runtime/Physics/Capsule.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

// 相手の面の上の位置と、段の決まりの並びと、赤の欄の置き場 (MapObj の部品 HitZones) を縛る

namespace
{
    using NS::Core::Vector3;
    using NS::Game::Level::HitFace;
    using NS::Game::Level::HitFaceJudgement;
    using NS::Game::Level::HitTier;
    using NS::Game::Level::HitZones;
    using NS::Game::Level::JudgeHitFace;
    using NS::Obj::HitSensorShape;
    using NS::Obj::SensorVolume;

    // 同梱の場面の自機の半径
    constexpr float k_PlayerRadius = 0.65f;
    constexpr float k_Tolerance = 1.0e-4f;
    // 半径 0.5 m の玉へ半径 0.65 m の自機が当たる時の、面の左右と上下の半分の幅
    constexpr float k_BallReach = 0.5f + k_PlayerRadius;

    // 原点の半径 0.5 m の玉へ、-x から +x へ進む線。+x へ進む時の右は -z なので、lateral が正なら右へずれた線
    HitFaceJudgement JudgeAlongX(const HitFace& face, float lateral)
    {
        HitFaceJudgement result;
        EXPECT_TRUE(JudgeHitFace(face,
                                 SensorVolume::Sphere(Vector3{0.0f, 0.0f, 0.0f}, 0.5f),
                                 Vector3{-3.0f, 0.0f, -lateral},
                                 Vector3{1.0f, 0.0f, 0.0f},
                                 k_PlayerRadius,
                                 result));
        return result;
    }

    // 半径 0.5 m の玉へ半径 0.5 m の自機。届く幅がちょうど 1 になり、面の上の位置がずれの長さそのものになる
    HitFaceJudgement JudgeOnUnitFace(const HitFace& face, float u, float v)
    {
        HitFaceJudgement result;
        EXPECT_TRUE(JudgeHitFace(face,
                                 SensorVolume::Sphere(Vector3{0.0f, 0.0f, 0.0f}, 0.5f),
                                 Vector3{-3.0f, v, -u},
                                 Vector3{1.0f, 0.0f, 0.0f},
                                 0.5f,
                                 result));
        return result;
    }

    NS::Obj::Actor* PlaceRocks(NS::Obj::Scene& scene, const nlohmann::json& overriddenParts)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        for (std::uint32_t id = 1; id <= 2; ++id)
        {
            nlohmann::json rock = NS::Obj::MakeObjectJson();
            NS::Obj::SetObjectJsonClass(rock, "MapObj");
            NS::Obj::SetObjectJsonId(rock, id);
            NS::Obj::SetObjectPosition(rock, Vector3{static_cast<float>(id) * 4.0f, 1.0f, 0.0f});
            if (id == 2)
            {
                // 位置は Transform の件に入っているので、件ごと置き換えずに足す
                for (nlohmann::json::const_iterator it = overriddenParts.begin(); it != overriddenParts.end(); ++it)
                {
                    NS::Obj::ObjectJsonParts(rock)[it.key()] = it.value();
                }
            }
            NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
        }
        scene.LoadJson(doc);
        return scene.Objects().FindByObjectId(2);
    }

    const HitZones* HitZonesOf(const NS::Obj::Actor& actor)
    {
        return NS::Obj::ComponentCast<HitZones>(actor.Part("HitZones"));
    }

    // 自機を根 (0, 1, 0) に、半径 0.5 m の置物を 1 m 先の高さ rockHeight に置く
    // 既定の 0.5 m は自機の玉の中心 (根 1 m − 半分の高さ 0.5 m) と同じ高さで、+z へ進む線は面の真ん中を通る
    Player* PlaceSlamTarget(NS::Obj::Scene& scene, const nlohmann::json& rockParts, float rockHeight = 0.5f)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json entry = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(entry, "Player");
        NS::Obj::SetObjectJsonId(entry, 1);
        NS::Obj::SetObjectPosition(entry, Vector3{0.0f, 1.0f, 0.0f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(entry));
        nlohmann::json rock = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(rock, "MapObj");
        NS::Obj::SetObjectJsonId(rock, 2);
        NS::Obj::SetObjectPosition(rock, Vector3{0.0f, rockHeight, 1.0f});
        for (nlohmann::json::const_iterator it = rockParts.begin(); it != rockParts.end(); ++it)
        {
            NS::Obj::ObjectJsonParts(rock)[it.key()] = it.value();
        }
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
        scene.LoadJson(doc);
        return NS::Obj::Cast<Player>(scene.Objects().FindByObjectId(1));
    }

    // 溜め 0 で +z へ突進させ、裁定を 1 回回す。溜め 0 のチャージ倍率は 1
    const NS::Game::Level::ImpactRecord& SlamOnce(Player& player)
    {
        player.RequestBodySlam(0.0f, Vector3{0.0f, 0.0f, 1.0f});
        EXPECT_TRUE(player.BodySlam());
        player.Resolver().OnUpdate();
        return player.Resolver().LastImpact();
    }
} // namespace

// 面の上の位置は、相手の中心から線までの左右と上下のずれを「半幅 + 自機の半径」「半分の高さ + 自機の半径」で割った物
// 自機から見て右が正。+x へ進む時の右は -z
TEST(HitZonesTest, FacePositionIsTheOffsetOverTheReach)
{
    HitFaceJudgement result;
    ASSERT_TRUE(JudgeHitFace(HitFace{},
                             SensorVolume::Sphere(Vector3{0.0f, 1.0f, 0.0f}, 0.5f),
                             Vector3{-3.0f, 1.3f, -0.4f},
                             Vector3{1.0f, 0.0f, 0.0f},
                             k_PlayerRadius,
                             result));
    EXPECT_NEAR(result.u, 0.4f / k_BallReach, k_Tolerance);
    EXPECT_NEAR(result.v, 0.3f / k_BallReach, k_Tolerance);
}

// 面はいつも自機が来る向きを向く。どの向きから当てても、同じ右と上のずれなら面の上の同じ位置
TEST(HitZonesTest, FaceTurnsTowardTheIncomingDirection)
{
    const Vector3 center{2.0f, 0.0f, -1.0f};
    for (int i = 0; i < 8; ++i)
    {
        SCOPED_TRACE(i);
        const float angle = static_cast<float>(i) * 0.25f * 3.14159265f;
        const Vector3 direction{std::cos(angle), 0.0f, std::sin(angle)};
        // 左手系で上から見た、進む向きの右
        const Vector3 right{direction.z, 0.0f, -direction.x};
        HitFaceJudgement result;
        ASSERT_TRUE(JudgeHitFace(HitFace{},
                                 SensorVolume::Sphere(center, 0.5f),
                                 center - direction * 3.0f + right * 0.3f + Vector3{0.0f, -0.2f, 0.0f},
                                 direction,
                                 k_PlayerRadius,
                                 result));
        EXPECT_NEAR(result.u, 0.3f / k_BallReach, k_Tolerance);
        EXPECT_NEAR(result.v, -0.2f / k_BallReach, k_Tolerance);
    }
}

// 同じ割合のずれは、相手の大きさが違っても面の上の同じ位置に入り、同じ段になる
TEST(HitZonesTest, SameShareLandsOnTheSamePlaceOfAnySizedTarget)
{
    const float largeReach = 1.5f + k_PlayerRadius;
    for (const float share : {0.3f, 0.6f})
    {
        SCOPED_TRACE(share);
        HitFaceJudgement onSmall;
        HitFaceJudgement onLarge;
        ASSERT_TRUE(JudgeHitFace(HitFace{},
                                 SensorVolume::Sphere(Vector3{0.0f, 0.0f, 0.0f}, 0.5f),
                                 Vector3{-3.0f, 0.0f, -share * k_BallReach},
                                 Vector3{1.0f, 0.0f, 0.0f},
                                 k_PlayerRadius,
                                 onSmall));
        ASSERT_TRUE(JudgeHitFace(HitFace{},
                                 SensorVolume::Sphere(Vector3{0.0f, 0.0f, 0.0f}, 1.5f),
                                 Vector3{-6.0f, 0.0f, -share * largeReach},
                                 Vector3{1.0f, 0.0f, 0.0f},
                                 k_PlayerRadius,
                                 onLarge));
        EXPECT_NEAR(onSmall.u, share, k_Tolerance);
        EXPECT_NEAR(onLarge.u, share, k_Tolerance);
        EXPECT_EQ(onSmall.tier, onLarge.tier);
    }
}

// 半幅と半分の高さは体の形から出す。カプセルの半分の高さは半径 + 筒の半分
TEST(HitZonesTest, CapsuleHalfHeightAddsItsTube)
{
    const SensorVolume capsule = SensorVolume::Capsule(NS::Phys::Capsule{
        .center = Vector3{0.0f, 1.0f, 0.0f}, .axis = Vector3{0.0f, 1.0f, 0.0f}, .halfHeight = 0.8f, .radius = 0.4f});
    HitFaceJudgement result;
    ASSERT_TRUE(JudgeHitFace(
        HitFace{}, capsule, Vector3{-3.0f, 1.6f, -0.3f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, result));
    EXPECT_NEAR(result.u, 0.3f / (0.4f + k_PlayerRadius), k_Tolerance);
    EXPECT_NEAR(result.v, 0.6f / (0.4f + 0.8f + k_PlayerRadius), k_Tolerance);
    EXPECT_EQ(result.bodyShape, HitSensorShape::Capsule);
}

// 箱の半分の高さは、向きの付いた箱の 3 軸を縦へ写した長さの和
TEST(HitZonesTest, BoxHalfHeightComesFromItsAxes)
{
    const SensorVolume box = SensorVolume::Box(
        NS::Core::MakeOBB(Vector3{0.0f, 0.0f, 0.0f}, NS::Core::Quaternion::Identity, Vector3{1.0f, 0.5f, 0.2f}));
    HitFaceJudgement result;
    ASSERT_TRUE(
        JudgeHitFace(HitFace{}, box, Vector3{-3.0f, 0.5f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, result));
    EXPECT_NEAR(result.v, 0.5f / (0.5f + k_PlayerRadius), k_Tolerance);
    EXPECT_EQ(result.bodyShape, HitSensorShape::Box);
}

// 回した箱は、箱を線に直交する軸へ写した半幅で届く幅を出す
// 外接箱で測ると、細い板の長い辺に沿って通る線でも板の対角ぶん広く出る
TEST(HitZonesTest, RotatedBoxUsesItsOwnWidthAcrossTheLine)
{
    const NS::Core::OBB obb =
        NS::Core::MakeOBB(Vector3{0.0f, 0.0f, 0.0f},
                          NS::Core::Quaternion::CreateFromAxisAngle(Vector3{0.0f, 1.0f, 0.0f}, 0.25f * 3.14159265f),
                          Vector3{1.0f, 0.5f, 0.2f});
    // 板の長い辺に沿って進み、板の厚みの向きへ 0.5 m ずれた線
    HitFaceJudgement result;
    ASSERT_TRUE(JudgeHitFace(HitFace{},
                             SensorVolume::Box(obb),
                             obb.center - obb.axisX * 3.0f + obb.axisZ * 0.5f,
                             obb.axisX,
                             k_PlayerRadius,
                             result));
    EXPECT_NEAR(result.ratio, 0.5f / (0.2f + k_PlayerRadius), k_Tolerance);
}

// 球の相手の形の種類は球
TEST(HitZonesTest, SphereBodyIsReportedAsSphere)
{
    EXPECT_EQ(JudgeAlongX(HitFace{}, 0.0f).bodyShape, HitSensorShape::Sphere);
}

// 体の形が球・カプセル・箱のどれでもない時は判定しない。半径の無い線分と、非数の形
TEST(HitZonesTest, BodyThatIsNoShapeIsNotJudged)
{
    HitFaceJudgement result;
    EXPECT_FALSE(JudgeHitFace(
        HitFace{}, SensorVolume{}, Vector3{-3.0f, 0.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, result));
    const SensorVolume broken =
        SensorVolume::Sphere(Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}, 0.5f);
    EXPECT_FALSE(
        JudgeHitFace(HitFace{}, broken, Vector3{-3.0f, 0.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, result));
}

// 水平の向きが無い線と、非数の起点・自機の半径は判定しない
TEST(HitZonesTest, LineWithoutHorizontalDirectionOrFiniteInputIsNotJudged)
{
    const SensorVolume ball = SensorVolume::Sphere(Vector3{0.0f, 0.0f, 0.0f}, 0.5f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    HitFaceJudgement result;
    EXPECT_FALSE(
        JudgeHitFace(HitFace{}, ball, Vector3{0.0f, 3.0f, 0.0f}, Vector3{0.0f, -1.0f, 0.0f}, k_PlayerRadius, result));
    EXPECT_FALSE(
        JudgeHitFace(HitFace{}, ball, Vector3{-3.0f, nan, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, result));
    EXPECT_FALSE(JudgeHitFace(HitFace{}, ball, Vector3{-3.0f, 0.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, nan, result));
}

// 決まりの並びは今「赤の中 → 真ん中」の 1 つ。赤の中は真ん中で赤の威力の倍率、外は外れで残りの威力の倍率
// 段と威力は同じ決まりから出る
TEST(HitZonesTest, RedGivesTheCenterTierAndItsPowerElseWide)
{
    HitFace face;
    face.powerScale = 1.2f;
    face.remainderPowerScale = 0.6f;

    const HitFaceJudgement inside = JudgeAlongX(face, 0.4f);
    EXPECT_EQ(inside.tier, HitTier::Center);
    EXPECT_FLOAT_EQ(inside.powerScale, 1.2f);

    // 0.5 ÷ 1.15 ≒ 0.435 は 0.43 の外
    const HitFaceJudgement outside = JudgeAlongX(face, 0.5f);
    EXPECT_EQ(outside.tier, HitTier::Wide);
    EXPECT_FLOAT_EQ(outside.powerScale, 0.6f);
}

// 丸は横と縦の幅を半径にした楕円。箱の角は丸では外
TEST(HitZonesTest, RoundIsAnEllipseAndBoxIsARectangle)
{
    HitFace face;
    face.width = 0.5f;
    face.height = 0.25f;

    face.round = true;
    EXPECT_EQ(JudgeOnUnitFace(face, 0.0f, 0.0f).tier, HitTier::Center);
    EXPECT_EQ(JudgeOnUnitFace(face, 0.49f, 0.0f).tier, HitTier::Center);
    EXPECT_EQ(JudgeOnUnitFace(face, 0.0f, 0.24f).tier, HitTier::Center);
    // (0.4 / 0.5)² + (0.2 / 0.25)² = 1.28
    EXPECT_EQ(JudgeOnUnitFace(face, 0.4f, 0.2f).tier, HitTier::Wide);

    face.round = false;
    EXPECT_EQ(JudgeOnUnitFace(face, 0.4f, 0.2f).tier, HitTier::Center);
    EXPECT_EQ(JudgeOnUnitFace(face, -0.49f, -0.24f).tier, HitTier::Center);
    EXPECT_EQ(JudgeOnUnitFace(face, 0.0f, -0.3f).tier, HitTier::Wide);
}

// 縁ちょうどは外れ。丸でも箱でも
TEST(HitZonesTest, ExactEdgeIsWide)
{
    HitFace face;
    face.width = 0.5f;
    face.height = 0.25f;
    for (const bool round : {true, false})
    {
        SCOPED_TRACE(round);
        face.round = round;
        EXPECT_EQ(JudgeOnUnitFace(face, 0.5f, 0.0f).tier, HitTier::Wide);
        EXPECT_EQ(JudgeOnUnitFace(face, 0.0f, -0.25f).tier, HitTier::Wide);
    }
}

// 赤の位置を動かすと覆う所も動く。上へずらした赤は、同じ高さの線では外れ
TEST(HitZonesTest, RedCenterMovesTheCoveredPart)
{
    HitFace face;
    face.width = 0.2f;
    face.height = 0.2f;
    face.centerU = 0.5f;
    face.centerV = -0.5f;

    EXPECT_EQ(JudgeOnUnitFace(face, 0.0f, 0.0f).tier, HitTier::Wide);
    EXPECT_EQ(JudgeOnUnitFace(face, 0.5f, -0.5f).tier, HitTier::Center);
    // 赤の中心から右と上へ赤の幅の 0.75 倍。0.75² × 2 = 1.125 で丸の外、箱の中
    EXPECT_EQ(JudgeOnUnitFace(face, 0.65f, -0.35f).tier, HitTier::Wide);
    face.round = false;
    EXPECT_EQ(JudgeOnUnitFace(face, 0.65f, -0.35f).tier, HitTier::Center);
}

// 幅が 0 の向きがある赤はどこも覆わない。全部が外れ
TEST(HitZonesTest, ZeroWidthRedCoversNothing)
{
    for (const bool round : {true, false})
    {
        SCOPED_TRACE(round);
        HitFace face;
        face.round = round;
        face.width = 0.0f;
        EXPECT_EQ(JudgeOnUnitFace(face, 0.0f, 0.0f).tier, HitTier::Wide);
        face.width = 0.43f;
        face.height = 0.0f;
        EXPECT_EQ(JudgeOnUnitFace(face, 0.0f, 0.0f).tier, HitTier::Wide);
    }
}

// 赤の欄の非数は 0 として読む。幅が非数なら覆わず、位置が非数なら真ん中、威力の倍率が非数なら 0
TEST(HitZonesTest, NanFieldsReadAsZero)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    HitFace face;
    face.width = nan;
    EXPECT_EQ(JudgeOnUnitFace(face, 0.0f, 0.0f).tier, HitTier::Wide);

    face = HitFace{};
    face.centerU = nan;
    face.centerV = nan;
    face.powerScale = nan;
    const HitFaceJudgement center = JudgeOnUnitFace(face, 0.0f, 0.0f);
    EXPECT_EQ(center.tier, HitTier::Center);
    EXPECT_FLOAT_EQ(center.powerScale, 0.0f);

    face = HitFace{};
    face.remainderPowerScale = nan;
    EXPECT_FLOAT_EQ(JudgeOnUnitFace(face, 0.9f, 0.0f).powerScale, 0.0f);
}

// 線の起点がもう相手に触れる所まで来ていても、線が触れる球に入る所で決める
// 予測は遠くから、当たりは触れてから呼ぶ。今の位置で決めると、同じ線でも予測と当たりが割れる
TEST(HitZonesTest, BallAlreadyTouchingIsJudgedWhereTheLineEntered)
{
    const SensorVolume ball = SensorVolume::Sphere(Vector3{0.0f, 0.0f, 0.0f}, 0.5f);
    HitFaceJudgement distant;
    HitFaceJudgement touching;
    ASSERT_TRUE(
        JudgeHitFace(HitFace{}, ball, Vector3{-3.0f, 0.0f, 0.5f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, distant));
    ASSERT_TRUE(
        JudgeHitFace(HitFace{}, ball, Vector3{-0.9f, 0.0f, 0.5f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, touching));
    EXPECT_EQ(touching.tier, distant.tier);
    EXPECT_NEAR(touching.u, distant.u, k_Tolerance);
    EXPECT_NEAR(touching.surfacePoint.x, distant.surfacePoint.x, k_Tolerance);
    EXPECT_NEAR(touching.surfacePoint.z, distant.surfacePoint.z, k_Tolerance);
}

// 触れる点は相手の表面の上。届かない線は、線に一番近い表面の点
TEST(HitZonesTest, SurfacePointIsWhereTheBallTouches)
{
    const SensorVolume ball = SensorVolume::Sphere(Vector3{0.0f, 1.0f, 0.0f}, 0.5f);
    HitFaceJudgement result;
    ASSERT_TRUE(
        JudgeHitFace(HitFace{}, ball, Vector3{-3.0f, 1.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, result));
    EXPECT_NEAR(result.surfacePoint.x, -0.5f, k_Tolerance);
    EXPECT_NEAR(result.surfacePoint.y, 1.0f, k_Tolerance);
    EXPECT_NEAR(result.surfacePoint.z, 0.0f, k_Tolerance);

    ASSERT_TRUE(
        JudgeHitFace(HitFace{}, ball, Vector3{-3.0f, 1.0f, 2.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, result));
    EXPECT_EQ(result.tier, HitTier::Wide);
    EXPECT_NEAR(result.surfacePoint.x, 0.0f, k_Tolerance);
    EXPECT_NEAR(result.surfacePoint.y, 1.0f, k_Tolerance);
    EXPECT_NEAR(result.surfacePoint.z, 0.5f, k_Tolerance);
}

// カプセルの触れる点は、線の高さに一番近い筒の上の点から見た表面
TEST(HitZonesTest, CapsuleSurfacePointIsOnItsTube)
{
    const SensorVolume capsule = SensorVolume::Capsule(NS::Phys::Capsule{
        .center = Vector3{0.0f, 1.0f, 0.0f}, .axis = Vector3{0.0f, 1.0f, 0.0f}, .halfHeight = 0.8f, .radius = 0.4f});
    HitFaceJudgement result;
    ASSERT_TRUE(JudgeHitFace(
        HitFace{}, capsule, Vector3{-3.0f, 1.5f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, result));
    EXPECT_NEAR(result.surfacePoint.x, -0.4f, k_Tolerance);
    EXPECT_NEAR(result.surfacePoint.y, 1.5f, k_Tolerance);
    EXPECT_NEAR(result.surfacePoint.z, 0.0f, k_Tolerance);
}

// 横ずれは中心を通る線で 0、かすめる線で 1 に近い。届かない線は比が 1 を超え、横ずれは 1 で止まる
TEST(HitZonesTest, OffsetIsZeroThroughTheCenterAndOneAtTheGrazingEdge)
{
    EXPECT_NEAR(JudgeAlongX(HitFace{}, 0.0f).offset01, 0.0f, k_Tolerance);
    EXPECT_GT(JudgeAlongX(HitFace{}, k_BallReach - 0.001f).offset01, 0.99f);

    const HitFaceJudgement missed = JudgeAlongX(HitFace{}, 2.0f);
    EXPECT_FLOAT_EQ(missed.offset01, 1.0f);
    EXPECT_NEAR(missed.ratio, 2.0f / k_BallReach, k_Tolerance);
}

// 線の通った点は、相手の中心を線へ垂直に落とした点を中心の高さに置いた物
TEST(HitZonesTest, LinePointIsTheCenterDroppedOntoTheLineAtTheCenterHeight)
{
    HitFaceJudgement result;
    ASSERT_TRUE(JudgeHitFace(HitFace{},
                             SensorVolume::Sphere(Vector3{0.0f, 1.0f, 0.0f}, 0.5f),
                             Vector3{-3.0f, 0.2f, 0.7f},
                             Vector3{1.0f, 0.0f, 0.0f},
                             k_PlayerRadius,
                             result));
    EXPECT_NEAR(result.linePoint.x, 0.0f, k_Tolerance);
    EXPECT_NEAR(result.linePoint.y, 1.0f, k_Tolerance);
    EXPECT_NEAR(result.linePoint.z, 0.7f, k_Tolerance);
    EXPECT_NEAR(result.along, 3.0f, k_Tolerance);
}

// 欄の既定は丸 0.43 の赤、真ん中、威力は減らさず、外れは 0.7
TEST(HitZonesTest, FieldDefaultsAreARoundRedOfPoint43)
{
    const HitZones zones;
    const nlohmann::json fields = NS::Obj::SerializeComponent(zones)["fields"];
    EXPECT_EQ(fields["丸"], true);
    EXPECT_FLOAT_EQ(fields["横幅"].get<float>(), 0.43f);
    EXPECT_FLOAT_EQ(fields["縦の幅"].get<float>(), 0.43f);
    EXPECT_FLOAT_EQ(fields["左右の位置"].get<float>(), 0.0f);
    EXPECT_FLOAT_EQ(fields["上下の位置"].get<float>(), 0.0f);
    EXPECT_FLOAT_EQ(fields["威力の倍率"].get<float>(), 1.0f);
    EXPECT_FLOAT_EQ(fields["残りの威力の倍率"].get<float>(), 0.7f);
    EXPECT_EQ(fields.size(), 7u);
}

// 幅は 0〜1、位置は -1〜1 へ丸める。威力の倍率は負を 0 にする。非数と無限は 0
TEST(HitZonesTest, FieldsAreClamped)
{
    HitZones zones;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(NS::Obj::ApplyJsonFields(zones,
                                       {{"横幅", 1.5f},
                                        {"縦の幅", -0.5f},
                                        {"左右の位置", -2.0f},
                                        {"上下の位置", 3.0f},
                                        {"威力の倍率", -1.0f},
                                        {"残りの威力の倍率", 1.5f}}),
              0u);
    EXPECT_FLOAT_EQ(zones.Face().width, 1.0f);
    EXPECT_FLOAT_EQ(zones.Face().height, 0.0f);
    EXPECT_FLOAT_EQ(zones.Face().centerU, -1.0f);
    EXPECT_FLOAT_EQ(zones.Face().centerV, 1.0f);
    EXPECT_FLOAT_EQ(zones.Face().powerScale, 0.0f);
    EXPECT_FLOAT_EQ(zones.Face().remainderPowerScale, 1.5f);

    zones.SetWidth(nan);
    zones.SetCenterV(std::numeric_limits<float>::infinity());
    zones.SetRemainderPowerScale(nan);
    EXPECT_FLOAT_EQ(zones.Face().width, 0.0f);
    EXPECT_FLOAT_EQ(zones.Face().centerV, 0.0f);
    EXPECT_FLOAT_EQ(zones.Face().remainderPowerScale, 0.0f);
}

// 種類の既定値 MapObj.json が全部の置物の赤の既定を書く
TEST(HitZonesTest, MapObjArchetypeWritesTheRedDefaults)
{
    const nlohmann::json* archetype = NS::Obj::ArchetypeLibrary::Get().Find("MapObj");
    ASSERT_NE(archetype, nullptr);
    const nlohmann::json* fields = NS::Obj::PartFields(*archetype, "HitZones");
    ASSERT_NE(fields, nullptr);
    EXPECT_EQ((*fields)["丸"], true);
    EXPECT_FLOAT_EQ(NS::Obj::FieldFloat(*fields, "横幅", -1.0f), 0.43f);
    EXPECT_FLOAT_EQ(NS::Obj::FieldFloat(*fields, "縦の幅", -1.0f), 0.43f);
    EXPECT_FLOAT_EQ(NS::Obj::FieldFloat(*fields, "左右の位置", -1.0f), 0.0f);
    EXPECT_FLOAT_EQ(NS::Obj::FieldFloat(*fields, "上下の位置", -1.0f), 0.0f);
    EXPECT_FLOAT_EQ(NS::Obj::FieldFloat(*fields, "威力の倍率", -1.0f), 1.0f);
    EXPECT_FLOAT_EQ(NS::Obj::FieldFloat(*fields, "残りの威力の倍率", -1.0f), 0.7f);
}

// 置物は部品 HitZones をコードで持ち、名前 "HitZones" で出す。体当たりの答えに赤の欄の写しと体の世界の形が載る
TEST(HitZonesTest, MapObjAnswersWithItsFaceAndBody)
{
    NS::Obj::Scene scene;
    NS::Obj::Actor* rock = PlaceRocks(scene, {{"HitZones", {{"左右の位置", 0.25f}, {"威力の倍率", 1.1f}}}});
    ASSERT_NE(rock, nullptr);
    const HitZones* zones = HitZonesOf(*rock);
    ASSERT_NE(zones, nullptr);

    NS::Game::Level::TackleTargetAnswer answer{};
    ASSERT_TRUE(NS::Game::Level::SendMsgAskTackleTarget(*rock->BodySensorPart(), answer));
    EXPECT_EQ(answer.face.round, zones->Face().round);
    EXPECT_FLOAT_EQ(answer.face.width, zones->Face().width);
    EXPECT_FLOAT_EQ(answer.face.centerU, 0.25f);
    EXPECT_FLOAT_EQ(answer.face.powerScale, 1.1f);
    EXPECT_FLOAT_EQ(answer.face.remainderPowerScale, zones->Face().remainderPowerScale);

    const SensorVolume expected = rock->BodySensorPart()->WorldVolume();
    EXPECT_FALSE(answer.body.isBox);
    EXPECT_FLOAT_EQ(answer.body.radius, expected.radius);
    EXPECT_FLOAT_EQ(answer.body.a.x, 8.0f);
    EXPECT_FLOAT_EQ(answer.body.a.y, expected.a.y);
    EXPECT_FLOAT_EQ(answer.body.b.x, expected.b.x);
}

// 個体で赤の位置を上書きすると、同じ線でもその相手だけ段が変わる
TEST(HitZonesTest, InstanceOverrideChangesOnlyThatTarget)
{
    NS::Obj::Scene scene;
    NS::Obj::Actor* moved = PlaceRocks(scene, {{"HitZones", {{"上下の位置", 0.8f}, {"縦の幅", 0.15f}}}});
    ASSERT_NE(moved, nullptr);
    NS::Obj::Actor* plain = scene.Objects().FindByObjectId(1);
    ASSERT_NE(plain, nullptr);

    const auto judgeThroughCenter = [&](NS::Obj::Actor& target) {
        NS::Game::Level::TackleTargetAnswer answer{};
        EXPECT_TRUE(NS::Game::Level::SendMsgAskTackleTarget(*target.BodySensorPart(), answer));
        const Vector3 center = answer.body.Center();
        HitFaceJudgement result;
        EXPECT_TRUE(JudgeHitFace(answer.face,
                                 answer.body,
                                 center - Vector3{3.0f, 0.0f, 0.0f},
                                 Vector3{1.0f, 0.0f, 0.0f},
                                 k_PlayerRadius,
                                 result));
        return result.tier;
    };
    EXPECT_EQ(judgeThroughCenter(*plain), HitTier::Center);
    EXPECT_EQ(judgeThroughCenter(*moved), HitTier::Wide);
}

// 個体の上書きは保存・再読込の往復で残る。保存に書くのは種類の既定と違う欄だけ
TEST(HitZonesTest, InstanceOverrideSurvivesSaveAndLoad)
{
    nlohmann::json saved;
    {
        NS::Obj::Scene scene;
        NS::Obj::Actor* rock = PlaceRocks(scene, {{"HitZones", {{"丸", false}, {"上下の位置", 0.3f}}}});
        ASSERT_NE(rock, nullptr);
        saved = scene.ToJson();
    }
    const nlohmann::json& objects = NS::Obj::SceneJsonObjects(saved);
    const nlohmann::json* plainFields =
        NS::Obj::PartFields(objects[NS::Obj::FindObjectIndexById(saved, 1)], "HitZones");
    if (plainFields != nullptr)
    {
        EXPECT_FALSE(NS::Obj::HasField(*plainFields, "上下の位置"));
        EXPECT_FALSE(NS::Obj::HasField(*plainFields, "丸"));
    }
    const nlohmann::json* overriddenFields =
        NS::Obj::PartFields(objects[NS::Obj::FindObjectIndexById(saved, 2)], "HitZones");
    ASSERT_NE(overriddenFields, nullptr);
    EXPECT_FLOAT_EQ(NS::Obj::FieldFloat(*overriddenFields, "上下の位置", 0.0f), 0.3f);
    EXPECT_FALSE(NS::Obj::HasField(*overriddenFields, "横幅"));

    NS::Obj::Scene reloaded;
    reloaded.LoadJson(saved);
    const NS::Obj::Actor* rock = reloaded.Objects().FindByObjectId(2);
    ASSERT_NE(rock, nullptr);
    const HitZones* zones = HitZonesOf(*rock);
    ASSERT_NE(zones, nullptr);
    EXPECT_FALSE(zones->Face().round);
    EXPECT_FLOAT_EQ(zones->Face().centerV, 0.3f);
    EXPECT_FLOAT_EQ(zones->Face().width, 0.43f);
    const NS::Obj::Actor* plain = reloaded.Objects().FindByObjectId(1);
    ASSERT_NE(plain, nullptr);
    ASSERT_NE(HitZonesOf(*plain), nullptr);
    EXPECT_TRUE(HitZonesOf(*plain)->Face().round);
    EXPECT_FLOAT_EQ(HitZonesOf(*plain)->Face().centerV, 0.0f);
}

// 面は相手の正面に接する平面に立ち、自機の方を向く。横と縦の半分の幅は判定と同じ「半幅 + 自機の半径」
TEST(HitZonesTest, FaceFrameStandsOnTheFrontOfTheBodyFacingThePlayer)
{
    NS::Game::Level::HitFaceFrame frame;
    ASSERT_TRUE(NS::Game::Level::MakeHitFaceFrame(
        SensorVolume::Sphere(Vector3{0.0f, 0.0f, 0.0f}, 0.5f), Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, frame));
    EXPECT_NEAR(frame.center.x, -0.5f, k_Tolerance);
    EXPECT_NEAR(frame.center.y, 0.0f, k_Tolerance);
    EXPECT_NEAR(frame.center.z, 0.0f, k_Tolerance);
    // +x へ進む時の右は -z
    EXPECT_NEAR(frame.right.z, -1.0f, k_Tolerance);
    EXPECT_NEAR(frame.up.y, 1.0f, k_Tolerance);
    EXPECT_NEAR(frame.normal.x, -1.0f, k_Tolerance);
    EXPECT_NEAR(frame.reachU, k_BallReach, k_Tolerance);
    EXPECT_NEAR(frame.reachV, k_BallReach, k_Tolerance);
    EXPECT_EQ(frame.bodyShape, HitSensorShape::Sphere);

    const SensorVolume capsule = SensorVolume::Capsule(NS::Phys::Capsule{
        .center = Vector3{0.0f, 1.0f, 0.0f}, .axis = Vector3{0.0f, 1.0f, 0.0f}, .halfHeight = 0.8f, .radius = 0.4f});
    ASSERT_TRUE(NS::Game::Level::MakeHitFaceFrame(capsule, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, frame));
    EXPECT_NEAR(frame.center.x, -0.4f, k_Tolerance);
    EXPECT_NEAR(frame.center.y, 1.0f, k_Tolerance);
    EXPECT_NEAR(frame.reachU, 0.4f + k_PlayerRadius, k_Tolerance);
    EXPECT_NEAR(frame.reachV, 0.4f + 0.8f + k_PlayerRadius, k_Tolerance);

    // 箱は奥行きの軸を線の向きへ写した長さだけ手前に立つ
    NS::Core::OBB box{};
    box.center = Vector3{0.0f, 1.0f, 0.0f};
    box.halfExtentX = 1.0f;
    box.halfExtentY = 0.5f;
    box.halfExtentZ = 2.0f;
    ASSERT_TRUE(
        NS::Game::Level::MakeHitFaceFrame(SensorVolume::Box(box), Vector3{0.0f, 0.0f, 1.0f}, k_PlayerRadius, frame));
    EXPECT_NEAR(frame.center.z, -2.0f, k_Tolerance);
    EXPECT_NEAR(frame.reachU, 1.0f + k_PlayerRadius, k_Tolerance);
    EXPECT_NEAR(frame.reachV, 0.5f + k_PlayerRadius, k_Tolerance);
    EXPECT_EQ(frame.bodyShape, HitSensorShape::Box);

    EXPECT_FALSE(NS::Game::Level::MakeHitFaceFrame(
        SensorVolume::Sphere(Vector3{0.0f, 0.0f, 0.0f}, 0.5f), Vector3{0.0f, 1.0f, 0.0f}, k_PlayerRadius, frame));
}

// 描く形は、外れの面を先に、段の決まりを優先の低い順に並べる。当てはまる段の色が上に見える
TEST(HitZonesTest, ShapesStackFromTheRemainderUpToTheHighestPriority)
{
    const std::vector<NS::Game::Level::HitFaceShape> onBall =
        NS::Game::Level::HitFaceShapes(HitFace{}, HitSensorShape::Sphere);
    ASSERT_EQ(onBall.size(), 2u);
    EXPECT_EQ(onBall.front().tier, HitTier::Wide);
    // 球の相手は触れられる丸の中だけが外れ
    EXPECT_TRUE(onBall.front().round);
    EXPECT_FLOAT_EQ(onBall.front().halfU, 1.0f);
    EXPECT_FLOAT_EQ(onBall.front().halfV, 1.0f);
    EXPECT_EQ(onBall.back().tier, HitTier::Center);
    EXPECT_TRUE(onBall.back().round);
    EXPECT_FLOAT_EQ(onBall.back().halfU, 0.43f);
    EXPECT_FLOAT_EQ(onBall.back().halfV, 0.43f);

    HitFace boxRed;
    boxRed.round = false;
    boxRed.centerU = 0.25f;
    boxRed.centerV = -0.5f;
    const std::vector<NS::Game::Level::HitFaceShape> onBox =
        NS::Game::Level::HitFaceShapes(boxRed, HitSensorShape::Box);
    ASSERT_EQ(onBox.size(), 2u);
    EXPECT_FALSE(onBox.front().round);
    EXPECT_FALSE(onBox.back().round);
    EXPECT_FLOAT_EQ(onBox.back().centerU, 0.25f);
    EXPECT_FLOAT_EQ(onBox.back().centerV, -0.5f);

    // 幅 0 の赤はどこも覆わないので描かない
    HitFace noRed;
    noRed.width = 0.0f;
    EXPECT_EQ(NS::Game::Level::HitFaceShapes(noRed, HitSensorShape::Sphere).size(), 1u);
}

// 描いた赤の縁の少し内側は判定でも赤、少し外側は外れ。描く形と判定が同じ値から出ている
TEST(HitZonesTest, RedOutlineAgreesWithTheJudgement)
{
    HitFace roundRed;
    roundRed.width = 0.43f;
    roundRed.height = 0.3f;
    roundRed.centerU = 0.2f;
    roundRed.centerV = -0.1f;
    HitFace boxRed;
    boxRed.round = false;
    boxRed.width = 0.5f;
    boxRed.height = 0.25f;
    boxRed.centerU = -0.3f;
    boxRed.centerV = 0.4f;
    const SensorVolume ball = SensorVolume::Sphere(Vector3{2.0f, 1.0f, -1.0f}, 0.5f);
    const SensorVolume capsule = SensorVolume::Capsule(NS::Phys::Capsule{
        .center = Vector3{-1.0f, 1.2f, 3.0f}, .axis = Vector3{0.0f, 1.0f, 0.0f}, .halfHeight = 0.6f, .radius = 0.4f});
    // 斜めの向きで、面の横の軸が世界の軸と揃わない場合も見る
    const Vector3 direction{0.6f, 0.0f, 0.8f};

    int checked = 0;
    for (const HitFace& face : {roundRed, boxRed})
    {
        for (const SensorVolume& body : {ball, capsule})
        {
            NS::Game::Level::HitFaceFrame frame;
            ASSERT_TRUE(NS::Game::Level::MakeHitFaceFrame(body, direction, k_PlayerRadius, frame));
            const std::vector<NS::Game::Level::HitFaceShape> shapes =
                NS::Game::Level::HitFaceShapes(face, frame.bodyShape);
            ASSERT_FALSE(shapes.empty());
            const NS::Game::Level::HitFaceShape& red = shapes.back();
            ASSERT_EQ(red.tier, HitTier::Center);
            const std::vector<NS::Core::Vector2> outline = NS::Game::Level::HitFaceShapeOutline(red);
            ASSERT_GE(outline.size(), 4u);
            for (const NS::Core::Vector2& edge : outline)
            {
                const NS::Core::Vector2 center{red.centerU, red.centerV};
                for (const float scale : {0.98f, 1.02f})
                {
                    const NS::Core::Vector2 at = center + (edge - center) * scale;
                    const Vector3 onFace = NS::Game::Level::HitFacePoint(frame, at.x, at.y);
                    HitFaceJudgement result;
                    ASSERT_TRUE(JudgeHitFace(face, body, onFace - direction * 4.0f, direction, k_PlayerRadius, result));
                    EXPECT_NEAR(result.u, at.x, k_Tolerance);
                    EXPECT_NEAR(result.v, at.y, k_Tolerance);
                    if (scale < 1.0f)
                    {
                        EXPECT_EQ(result.tier, HitTier::Center);
                    }
                    else
                    {
                        EXPECT_EQ(result.tier, HitTier::Wide);
                    }
                    ++checked;
                }
            }
        }
    }
    EXPECT_GT(checked, 0);
}

// 直近の当たりの記録に、自機の玉が相手の表面に触れた点が載る。エディタはこの点に印を描く
TEST(HitZonesTest, LastImpactRecordsWhereTheBallTouchedTheSurface)
{
    NS::Obj::Scene scene;
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    nlohmann::json entry = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(entry, "Player");
    NS::Obj::SetObjectJsonId(entry, 1);
    NS::Obj::SetObjectPosition(entry, Vector3{0.0f, 1.0f, 0.0f});
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(entry));
    nlohmann::json rock = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(rock, "MapObj");
    NS::Obj::SetObjectJsonId(rock, 2);
    NS::Obj::SetObjectPosition(rock, Vector3{0.0f, 1.0f, 1.0f});
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
    scene.LoadJson(doc);
    Player* player = NS::Obj::Cast<Player>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(player, nullptr);
    const NS::Obj::Actor* target = scene.Objects().FindByObjectId(2);
    ASSERT_NE(target, nullptr);
    const SensorVolume body = target->BodySensorPart()->WorldVolume();

    player->RequestBodySlam(0.0f, Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    player->Resolver().OnUpdate();
    const NS::Game::Level::ImpactRecord& impact = player->Resolver().LastImpact();
    ASSERT_EQ(impact.sequence, 1u);

    const Vector3 center = body.Center();
    EXPECT_NEAR((impact.surfacePoint - center).Length(), body.radius, k_Tolerance);
    // 自機の来た側の表面
    EXPECT_LT(impact.surfacePoint.z, center.z);
}

// 体の形が判定できない時は、中心近く・係数 1 にせず外れと残りの威力の倍率にする。触れた点は体の中心
TEST(HitZonesTest, BodyThatIsNoShapeFallsToWideWithTheRemainderPower)
{
    HitFace face;
    face.powerScale = 1.2f;
    face.remainderPowerScale = 0.6f;
    const SensorVolume broken = SensorVolume::Sphere(Vector3{1.0f, 2.0f, 3.0f}, 0.0f);
    const HitFaceJudgement fallen = NS::Game::Level::JudgeHitFaceOrWide(
        face, broken, Vector3{-3.0f, 2.0f, 3.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius);
    EXPECT_EQ(fallen.tier, HitTier::Wide);
    EXPECT_FLOAT_EQ(fallen.powerScale, 0.6f);
    EXPECT_FLOAT_EQ(fallen.offset01, 1.0f);
    EXPECT_FLOAT_EQ(fallen.surfacePoint.x, 1.0f);
    EXPECT_FLOAT_EQ(fallen.surfacePoint.y, 2.0f);
    EXPECT_FLOAT_EQ(fallen.surfacePoint.z, 3.0f);

    // 赤の外の倍率の非数は、判定と同じく 0 と読む
    face.remainderPowerScale = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FLOAT_EQ(NS::Game::Level::JudgeHitFaceOrWide(
                        face, broken, Vector3{-3.0f, 2.0f, 3.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius)
                        .powerScale,
                    0.0f);

    // 判定できる体では JudgeHitFace の結果そのもの
    const SensorVolume ball = SensorVolume::Sphere(Vector3{0.0f, 0.0f, 0.0f}, 0.5f);
    HitFaceJudgement judged;
    ASSERT_TRUE(
        JudgeHitFace(face, ball, Vector3{-3.0f, 0.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius, judged));
    const HitFaceJudgement passed = NS::Game::Level::JudgeHitFaceOrWide(
        face, ball, Vector3{-3.0f, 0.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_PlayerRadius);
    EXPECT_EQ(passed.tier, HitTier::Center);
    EXPECT_FLOAT_EQ(passed.powerScale, judged.powerScale);
    EXPECT_FLOAT_EQ(passed.surfacePoint.x, judged.surfacePoint.x);
}

// 裁定の段と威力の当たり位置の係数は、相手の面の同じ決まりから出る
// 赤の位置を個体で上書きすると、同じ線でもその相手だけ外れになる
TEST(HitZonesTest, VerdictTakesTheTierAndPowerFromTheFace)
{
    NS::Obj::Scene plainScene;
    Player* plain = PlaceSlamTarget(plainScene, {{"HitZones", {{"威力の倍率", 1.1f}, {"残りの威力の倍率", 0.6f}}}});
    ASSERT_NE(plain, nullptr);
    const NS::Game::Level::ImpactRecord& centered = SlamOnce(*plain);
    ASSERT_EQ(centered.sequence, 1u);
    EXPECT_EQ(centered.tier, HitTier::Center);
    EXPECT_FLOAT_EQ(centered.positionFactor, 1.1f);
    EXPECT_FLOAT_EQ(centered.power, 1.1f);
    EXPECT_TRUE(centered.centerHit);

    NS::Obj::Scene movedScene;
    Player* moved = PlaceSlamTarget(
        movedScene, {{"HitZones", {{"上下の位置", 0.8f}, {"縦の幅", 0.15f}, {"残りの威力の倍率", 0.6f}}}});
    ASSERT_NE(moved, nullptr);
    const NS::Game::Level::ImpactRecord& missed = SlamOnce(*moved);
    ASSERT_EQ(missed.sequence, 1u);
    EXPECT_EQ(missed.tier, HitTier::Wide);
    EXPECT_FLOAT_EQ(missed.positionFactor, 0.6f);
    EXPECT_FLOAT_EQ(missed.power, 0.6f);
    EXPECT_FALSE(missed.centerHit);
}

// 上下のずれも段に効く。赤の既定 0.43 に対し、玉の中心が相手の中心より 0.5 m 低い線は 0.5 ÷ 1.15 ≒ 0.435 で外
TEST(HitZonesTest, VerdictMissesWhenTheLinePassesBelowTheRed)
{
    NS::Obj::Scene scene;
    Player* player = PlaceSlamTarget(scene, nlohmann::json::object(), 1.0f);
    ASSERT_NE(player, nullptr);
    const NS::Game::Level::ImpactRecord& impact = SlamOnce(*player);
    ASSERT_EQ(impact.sequence, 1u);
    EXPECT_EQ(impact.tier, HitTier::Wide);
    EXPECT_FLOAT_EQ(impact.positionFactor, 0.7f);
    // 横にはずれていない
    EXPECT_NEAR(impact.offset01, 0.0f, k_Tolerance);
}

// 押している間の狙いの予測は、裁定と同じ面の判定で段と横ずれを出す
TEST(HitZonesTest, AimPredictionGivesTheSameTierAsTheVerdict)
{
    const nlohmann::json raised{{"HitZones", {{"上下の位置", 0.8f}, {"縦の幅", 0.15f}}}};
    const std::vector<std::pair<nlohmann::json, HitTier>> cases{{nlohmann::json::object(), HitTier::Center},
                                                                {raised, HitTier::Wide}};
    for (const std::pair<nlohmann::json, HitTier>& entry : cases)
    {
        NS::Obj::Scene scene;
        Player* player = PlaceSlamTarget(scene, entry.first);
        ASSERT_NE(player, nullptr);
        NS::Game::Level::SlamLineTarget predicted{};
        ASSERT_TRUE(player->Resolver().FindSlamLineTarget(Vector3{0.0f, 0.0f, 1.0f}, 10.0f, predicted));
        EXPECT_EQ(predicted.tier, entry.second);
        const NS::Game::Level::ImpactRecord& impact = SlamOnce(*player);
        ASSERT_EQ(impact.sequence, 1u);
        EXPECT_EQ(predicted.tier, impact.tier);
        EXPECT_FLOAT_EQ(predicted.offset, impact.offset01);
    }
}
