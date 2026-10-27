#include "Game/Level/ReboundDirections.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerParams.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/Phys/PhysicsScene.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <cmath>

// エアダッシュ (押した瞬間に一定の速さで飛ぶ) と、地形の面での反動 (操作なし → 補正とダッシュ 1 回 → 普段) を縛る

namespace
{
    // 欄の秒を固定ステップのフレーム数にする
    int FramesFor(float seconds)
    {
        return static_cast<int>(std::ceil(seconds / NS::OS::FrameTimer::FixedDelta()));
    }

    nlohmann::json ParamsFields(const PlayerActor* player)
    {
        return NS::SerializeClassObjFields(&player->Params());
    }

    // +Z を向くカメラと、宙に浮かせた自機。床は無い
    PlayerActor* PlaceFloatingPlayer(NS::Scene& scene)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json entry = NS::MakeActorJson();
        NS::SetActorJsonFile(entry, "PlayerActor");
        NS::SetActorJsonId(entry, 1);
        NS::SceneJsonActors(doc).push_back(std::move(entry));
        scene.ResetFromJson(doc);
        if (PlaceViewCamera(scene, NS::Vector3{0.0f, 5.0f, -5.0f}, NS::Vector3{0.0f, 5.0f, 0.0f}) == nullptr)
        {
            return nullptr;
        }
        PlayerActor* player = static_cast<PlayerActor*>(scene.Actors().FindByActorId(1));
        if (player != nullptr)
        {
            player->Root().SetPosition(NS::Vector3{0.0f, 5.0f, 0.0f});
        }
        return player;
    }

    void AddBox(NS::Scene& scene, const NS::Vector3& center, const NS::Vector3& half)
    {
        NS::OBB box;
        box.center = center;
        box.halfExtentX = half.x;
        box.halfExtentY = half.y;
        box.halfExtentZ = half.z;
        scene.Physics().AddBox(box, NS::Phys::ObjectLayers::Terrain);
    }

    // 自機の前 (+Z) の、面が z = 3 の大きな壁
    void AddFrontWall(NS::Scene& scene)
    {
        AddBox(scene, NS::Vector3{0.0f, 5.0f, 3.5f}, NS::Vector3{20.0f, 20.0f, 0.5f});
    }

    // +Z へ押した瞬間にダッシュし、放したスティックのまま壁へ当てる。反動へ移ったら true
    bool DashIntoFrontWall(PlayerActor* player)
    {
        player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
        player->Update(true);
        player->SetDesiredMove(NS::Vector3{}, 0.0f);
        for (int frame = 0; frame < 30; ++frame)
        {
            if (player->IsDashRebounding())
            {
                return true;
            }
            player->Update(true);
        }
        return player->IsDashRebounding();
    }

    float AngleBetween(const NS::Vector3& a, const NS::Vector3& b)
    {
        NS::Vector3 na = a;
        NS::Vector3 nb = b;
        na.Normalize();
        nb.Normalize();
        return std::acos(std::clamp(NS::Dot(na, nb), -1.0f, 1.0f));
    }
} // namespace

TEST(PlayerAirDashTest, PressFrameStartsTheDashAtTheDashSpeed)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const float distance = ParamsFields(player).at("エアダッシュの距離").get<float>();
    const float seconds = ParamsFields(player).at("エアダッシュの秒").get<float>();
    const float speed = distance / seconds;
    const float startX = player->Root().Position().x;

    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    EXPECT_TRUE(player->IsAirDashing());
    EXPECT_NEAR(player->Body().Velocity().x, speed, 1.0e-3f);
    EXPECT_NEAR(player->Body().Velocity().y, 0.0f, 1.0e-3f);

    const int frames = FramesFor(seconds);
    for (int frame = 0; frame < frames + 1 && player->IsAirDashing(); ++frame)
    {
        player->Update(true);
    }
    EXPECT_FALSE(player->IsAirDashing());
    EXPECT_EQ(player->AirDashesRemaining(), 0);
    // 秒はフレームの刻みで終わるので、1 フレームで進む分だけずれてよい
    EXPECT_NEAR(player->Root().Position().x - startX, distance, speed * NS::OS::FrameTimer::FixedDelta());
}

TEST(PlayerAirDashTest, TiltJustBeforeThePressKeepsTheDirectionHeldBefore)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const int hold = FramesFor(ParamsFields(player).at("行きたい向きとみなす秒").get<float>());

    player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    for (int frame = 0; frame < hold + 2; ++frame)
    {
        player->Update(false);
    }
    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    EXPECT_NEAR(player->Body().Velocity().x, 0.0f, 1.0e-3f);
    EXPECT_GT(player->Body().Velocity().z, 0.0f);
}

TEST(PlayerAirDashTest, HoldingTheNewDirectionLongerThanTheWindowDashesThatWay)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const int hold = FramesFor(ParamsFields(player).at("行きたい向きとみなす秒").get<float>());

    player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    for (int frame = 0; frame < hold + 2; ++frame)
    {
        player->Update(false);
    }
    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    for (int frame = 0; frame < hold + 2; ++frame)
    {
        player->Update(false);
    }
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    EXPECT_GT(player->Body().Velocity().x, 0.0f);
    EXPECT_NEAR(player->Body().Velocity().z, 0.0f, 1.0e-3f);
}

TEST(PlayerAirDashTest, ReleasingTheStickForgetsTheDirectionHeldBefore)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const int hold = FramesFor(ParamsFields(player).at("行きたい向きとみなす秒").get<float>());

    player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    for (int frame = 0; frame < hold + 2; ++frame)
    {
        player->Update(false);
    }
    player->SetDesiredMove(NS::Vector3{}, 0.0f);
    player->Update(false);
    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    EXPECT_GT(player->Body().Velocity().x, 0.0f);
    EXPECT_NEAR(player->Body().Velocity().z, 0.0f, 1.0e-3f);
}

TEST(PlayerAirDashTest, HittingAWallReboundsStraightBackAtTheFullSpeed)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const float speed = ParamsFields(player).at("エアダッシュの距離").get<float>() /
                        ParamsFields(player).at("エアダッシュの秒").get<float>() *
                        ParamsFields(player).at("返る速さのダッシュの速さに対する倍率").get<float>();

    ASSERT_TRUE(DashIntoFrontWall(player));
    const NS::Vector3 velocity = player->Body().Velocity();
    EXPECT_NEAR(velocity.x, 0.0f, 1.0e-3f);
    EXPECT_NEAR(velocity.y, 0.0f, 1.0e-3f);
    EXPECT_NEAR(velocity.z, -speed, 1.0e-3f);
}

TEST(PlayerAirDashTest, NoDashDuringTheLockThenOneDashInTheSteerStage)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const nlohmann::json fields = ParamsFields(player);
    ASSERT_TRUE(DashIntoFrontWall(player));

    // 操作なしの間の押しはダッシュにならない
    player->Update(false);
    player->Update(true);
    EXPECT_TRUE(player->IsDashRebounding());
    EXPECT_FALSE(player->IsAirDashing());

    // 当たりのヒットストップの間は反動の秒が進まないので、秒で待つ
    const float lock = fields.at("反動の操作なしの秒").get<float>();
    for (int frame = 0; frame < 60 && player->AirDash().ReboundElapsed() <= lock; ++frame)
    {
        player->Update(false);
    }
    player->Update(true);
    EXPECT_TRUE(player->IsAirDashing());

    // 補正の段で戻ったダッシュは 1 回だけ。使い切ったら次の押しは効かない
    for (int frame = 0; frame < FramesFor(fields.at("エアダッシュの秒").get<float>()) + 1; ++frame)
    {
        player->Update(false);
    }
    ASSERT_FALSE(player->IsAirDashing());
    player->Update(true);
    EXPECT_FALSE(player->IsAirDashing());
}

TEST(PlayerAirDashTest, GroundReboundStillLocksTheDashWhileTheCountIsBack)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    // 床に立たせてから壁へダッシュする。接地の間は回数が戻るので、止めるのは操作なしの段だけ
    AddBox(scene, NS::Vector3{0.0f, -0.5f, 0.0f}, NS::Vector3{50.0f, 0.5f, 50.0f});
    player->Root().SetPosition(NS::Vector3{0.0f, 0.9f, 0.0f});
    for (int frame = 0; frame < 5; ++frame)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->Body().IsGrounded());
    ASSERT_TRUE(DashIntoFrontWall(player));

    player->Update(false);
    EXPECT_EQ(player->AirDashesRemaining(), 1);
    player->Update(true);
    EXPECT_TRUE(player->IsDashRebounding());
    EXPECT_FALSE(player->IsAirDashing());
}

TEST(PlayerAirDashTest, LandingRestoresTheDash)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    // 自機の 5 m 下の床
    AddBox(scene, NS::Vector3{0.0f, -0.5f, 0.0f}, NS::Vector3{50.0f, 0.5f, 50.0f});

    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    player->SetDesiredMove(NS::Vector3{}, 0.0f);
    for (int frame = 0; frame < 240 && !player->Body().IsGrounded(); ++frame)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->Body().IsGrounded());
    player->Update(false);
    EXPECT_EQ(player->AirDashesRemaining(), 1);
}

TEST(PlayerAirDashTest, SteeringNeverTurnsMoreThanTheLimit)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const nlohmann::json fields = ParamsFields(player);
    ASSERT_TRUE(DashIntoFrontWall(player));
    // 操作なしの間に倒すとずらしになるので、操作なしの間が明けてから測る
    const float lock = fields.at("反動の操作なしの秒").get<float>();
    for (int frame = 0; frame < 60 && player->AirDash().ReboundElapsed() <= lock; ++frame)
    {
        player->Update(false);
    }
    // 補正の段の間ずっと真横へ倒す
    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    const int frames = FramesFor(fields.at("反動の補正が終わる秒").get<float>());
    float largest = 0.0f;
    for (int frame = 0; frame < frames && player->IsDashRebounding(); ++frame)
    {
        player->Update(false);
        // 曲げは速度の全部を回すので、真後ろへ返る縦の面から外れた角度で測る。重力と減速はこの角度を増やさない
        const NS::Vector3 velocity = player->Body().Velocity();
        largest = std::max(largest, std::asin(std::abs(velocity.x) / velocity.Length()));
    }
    const float limit = GL::Level::DirectionStepRadians(fields.at("向きの表の分割数").get<int>()) *
                        fields.at("曲げの上限の段の割合").get<float>();
    EXPECT_GT(largest, 0.0f);
    EXPECT_LE(largest, limit + 1.0e-3f);
}

TEST(PlayerAirDashTest, HoldingForwardThroughTheSteerStageBendsUpNotSideways)
{
    const auto steer = [](const NS::Vector3& desired, float scale) {
        NS::Scene scene;
        PlayerActor* player = PlaceFloatingPlayer(scene);
        AddFrontWall(scene);
        const nlohmann::json fields = ParamsFields(player);
        EXPECT_TRUE(DashIntoFrontWall(player));
        const float lock = fields.at("反動の操作なしの秒").get<float>();
        for (int frame = 0; frame < 60 && player->AirDash().ReboundElapsed() <= lock; ++frame)
        {
            player->Update(false);
        }
        player->SetDesiredMove(desired, scale);
        const float end = fields.at("反動の補正が終わる秒").get<float>();
        for (int frame = 0; frame < 120 && player->AirDash().ReboundElapsed() + 0.05f < end; ++frame)
        {
            player->Update(false);
        }
        return player->Body().Velocity();
    };
    const NS::Vector3 released = steer(NS::Vector3{}, 0.0f);
    NS::Vector3 forward{-0.3f, 0.0f, 1.0f};
    forward.Normalize();
    const NS::Vector3 held = steer(forward, 1.0f);

    const float sideways = std::atan2(std::abs(held.x), -held.z);
    EXPECT_LT(sideways, NS::ToRadians(NS::Degrees{10.0f}).value);
    EXPECT_GT(held.y, released.y);
}

// 地形の反動はカメラを回さず、回す入力も止めない。縦の向きは返った向きの縦の成分で渡し、ダッシュと反動の間は視野を広げる
TEST(PlayerAirDashTest, CameraStateDuringDashAndTerrainRebound)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);

    player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    const GL::Player::CameraTargetState dashing = player->GetCameraTargetState();
    EXPECT_TRUE(dashing.rebound.widenView);
    EXPECT_FALSE(dashing.rebound.rebounding);

    player->SetDesiredMove(NS::Vector3{}, 0.0f);
    for (int frame = 0; frame < 30 && !player->IsDashRebounding(); ++frame)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->IsDashRebounding());
    player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    for (int frame = 0; frame < 30 && player->AirDash().LastRebound().shifted.y <= 0.5f; ++frame)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->IsDashRebounding());
    ASSERT_GT(player->AirDash().LastRebound().shifted.y, 0.5f);
    const GL::Player::CameraTargetState rebounding = player->GetCameraTargetState();
    EXPECT_TRUE(rebounding.rebound.rebounding);
    EXPECT_TRUE(rebounding.rebound.widenView);
    EXPECT_FALSE(rebounding.rebound.holdLook);
    EXPECT_FLOAT_EQ(rebounding.rebound.slamDirection.Length(), 0.0f);
    EXPECT_FLOAT_EQ(rebounding.rebound.verticalLean, player->AirDash().LastRebound().shifted.y);

    player->ResetState();
    EXPECT_FALSE(player->GetCameraTargetState().rebound.widenView);
}

TEST(PlayerAirDashTest, TiltAfterTheHitShiftsTheReboundOneStep)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const float lock = ParamsFields(player).at("反動の操作なしの秒").get<float>();
    ASSERT_TRUE(DashIntoFrontWall(player));
    ASSERT_NEAR(player->AirDash().LastRebound().shifted.y, 0.0f, 1.0e-3f);

    player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    for (int frame = 0; frame < 60 && player->AirDash().ReboundElapsed() <= lock; ++frame)
    {
        player->Update(false);
    }
    const NS::Vector3 shifted = player->AirDash().LastRebound().shifted;
    EXPECT_NEAR(shifted.y, std::sqrt(0.5f), 1.0e-3f);
    EXPECT_NEAR(shifted.z, -std::sqrt(0.5f), 1.0e-3f);
    EXPECT_GT(player->Body().Velocity().y, 0.0f);
}

TEST(PlayerAirDashTest, HoldingTheDashStickThroughTheHitShiftsTheReboundOneStep)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const float lock = ParamsFields(player).at("反動の操作なしの秒").get<float>();

    player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    player->Update(true);
    for (int frame = 0; frame < 30 && !player->IsDashRebounding(); ++frame)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->IsDashRebounding());
    for (int frame = 0; frame < 60 && player->AirDash().ReboundElapsed() <= lock; ++frame)
    {
        player->Update(false);
    }
    const NS::Vector3 shifted = player->AirDash().LastRebound().shifted;
    EXPECT_NEAR(shifted.y, std::sqrt(0.5f), 1.0e-3f);
    EXPECT_NEAR(shifted.z, -std::sqrt(0.5f), 1.0e-3f);
}

TEST(PlayerAirDashTest, TiltAfterTheLockDoesNotShift)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const float lock = ParamsFields(player).at("反動の操作なしの秒").get<float>();
    ASSERT_TRUE(DashIntoFrontWall(player));
    for (int frame = 0; frame < 60 && player->AirDash().ReboundElapsed() <= lock; ++frame)
    {
        player->Update(false);
    }

    player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    for (int frame = 0; frame < 5; ++frame)
    {
        player->Update(false);
    }
    EXPECT_NEAR(player->AirDash().LastRebound().shifted.y, 0.0f, 1.0e-3f);
}

// 押したフレームに丸まる。立ち姿のままダッシュすると、飛び出した瞬間に玉に見えない
TEST(PlayerAirDashTest, PressFrameCurlsIntoTheBall)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    ASSERT_FALSE(player->IsCurled());

    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    EXPECT_TRUE(player->IsCurled());
}

TEST(PlayerAirDashTest, PressInTheAirKeepsTheBallAtTheBodyCenter)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    ASSERT_FALSE(player->Body().IsGrounded());
    const float before = player->Root().Position().y;

    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    ASSERT_TRUE(player->IsCurled());
    EXPECT_NEAR(player->Root().Position().y, before, 1.0e-3f);
}

// ダッシュの間も地形の反動の間も、玉は今の進む向きへ前転し、進んだ距離に合う角度だけ回る
TEST(PlayerAirDashTest, BallRollsTowardTheVelocityDuringTheDashAndTheRebound)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const float degreesPerMeter =
        NS::SerializeClassObjFields(&player->Appearance()).at("エアダッシュの 1 m で回る角度").get<float>();
    const float dt = NS::OS::FrameTimer::FixedDelta();

    player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    player->Update(true);
    player->SetDesiredMove(NS::Vector3{}, 0.0f);
    player->Update(false);
    ASSERT_TRUE(player->IsAirDashing());
    // +Z へ進む前転の軸は 上 × 前 = (1, 0, 0)
    const NS::Vector3 dashAxis = player->Appearance().SpinAxis();
    EXPECT_NEAR(dashAxis.x, 1.0f, 1.0e-3f);
    EXPECT_NEAR(dashAxis.z, 0.0f, 1.0e-3f);
    EXPECT_NEAR(player->Appearance().SpinDegreesThisFrame(),
                player->Body().Velocity().Length() * degreesPerMeter * dt,
                1.0e-2f);

    // 返った後、ヒットストップが明けたら返った向き (−Z) へ回る
    for (int frame = 0; frame < 60 && !(player->IsDashRebounding() && player->Motion().canMoveBody); ++frame)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->IsDashRebounding());
    player->Update(false);
    ASSERT_TRUE(player->IsDashRebounding());
    const NS::Vector3 reboundAxis = player->Appearance().SpinAxis();
    EXPECT_NEAR(reboundAxis.x, -1.0f, 1.0e-3f);
    EXPECT_NEAR(reboundAxis.z, 0.0f, 1.0e-3f);
    EXPECT_NEAR(player->Appearance().SpinDegreesThisFrame(),
                player->Body().Velocity().Length() * degreesPerMeter * dt,
                1.0e-2f);
}

// ダッシュが終わっても宙の間は玉のまま。着地して落ち着いたら立ち姿へ戻る
TEST(PlayerAirDashTest, BallStaysCurledInTheAirAndUncurlsAfterLanding)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddBox(scene, NS::Vector3{0.0f, -0.5f, 0.0f}, NS::Vector3{50.0f, 0.5f, 50.0f});

    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    player->SetDesiredMove(NS::Vector3{}, 0.0f);
    for (int frame = 0; frame < 60 && player->IsAirDashing(); ++frame)
    {
        player->Update(false);
    }
    ASSERT_FALSE(player->IsAirDashing());
    ASSERT_FALSE(player->Body().IsGrounded());
    EXPECT_TRUE(player->IsCurled());

    for (int frame = 0; frame < 240 && !player->Body().IsGrounded(); ++frame)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->Body().IsGrounded());
    player->Update(false);
    EXPECT_FALSE(player->IsCurled());
}

TEST(PlayerAirDashTest, ResetStateEndsTheReboundAndRestoresTheDash)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    ASSERT_TRUE(DashIntoFrontWall(player));

    player->ResetState();
    EXPECT_FALSE(player->IsDashRebounding());
    EXPECT_FALSE(player->IsAirDashing());
    EXPECT_EQ(player->AirDashesRemaining(), 1);
}

TEST(PlayerAirDashTest, AirControlComesBackGraduallyAfterTheDash)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const nlohmann::json fields = ParamsFields(player);
    const float fullDrag = fields.at("曲がる時の抵抗").get<float>() * NS::OS::FrameTimer::FixedDelta();

    player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    player->Update(true);
    player->SetDesiredMove(NS::Vector3{}, 0.0f);
    for (int frame = 0; frame < 60 && player->IsAirDashing(); ++frame)
    {
        player->Update(false);
    }
    ASSERT_FALSE(player->IsAirDashing());

    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    float before = player->Body().Velocity().z;
    player->Update(false);
    const float first = before - player->Body().Velocity().z;
    before = player->Body().Velocity().z;
    for (int frame = 0; frame < 4; ++frame)
    {
        before = player->Body().Velocity().z;
        player->Update(false);
    }
    const float later = before - player->Body().Velocity().z;

    EXPECT_LT(first, fullDrag * 0.25f);
    EXPECT_GT(later, first);
    EXPECT_LE(later, fullDrag + 1.0e-4f);
}

TEST(PlayerAirDashTest, PlainFallHasFullAirControlFromTheFirstFrame)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const float full =
        ParamsFields(player).at("空中の加速度").get<float>() * NS::OS::FrameTimer::FixedDelta();
    player->Update(false);
    player->Update(false);

    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    const float before = player->Body().Velocity().x;
    player->Update(false);
    EXPECT_NEAR(player->Body().Velocity().x - before, full, 1.0e-3f);
}
