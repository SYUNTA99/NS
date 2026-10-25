#include "Game/Level/ReboundDirections.h"
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
    const float speed = ParamsFields(player).at("エアダッシュの速さ").get<float>();

    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    EXPECT_TRUE(player->IsAirDashing());
    EXPECT_NEAR(player->Body().Velocity().x, speed, 1.0e-3f);
    EXPECT_NEAR(player->Body().Velocity().y, 0.0f, 1.0e-3f);

    // 押し続けても次のダッシュにはならない。ダッシュの秒が過ぎると終わる
    const int frames = FramesFor(ParamsFields(player).at("エアダッシュの秒").get<float>());
    for (int frame = 0; frame < frames + 1; ++frame)
    {
        player->Update(true);
    }
    EXPECT_FALSE(player->IsAirDashing());
    EXPECT_EQ(player->AirDashesRemaining(), 0);
}

TEST(PlayerAirDashTest, HittingAWallReboundsStraightBackAtTheFullSpeed)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const float speed = ParamsFields(player).at("エアダッシュの速さ").get<float>();

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
    const NS::Vector3 start = player->Body().LateralVelocity();

    // 補正の段の間ずっと真横へ倒す
    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    const int frames = FramesFor(fields.at("反動の補正が終わる秒").get<float>());
    float largest = 0.0f;
    for (int frame = 0; frame < frames && player->IsDashRebounding(); ++frame)
    {
        player->Update(false);
        largest = std::max(largest, AngleBetween(start, player->Body().LateralVelocity()));
    }
    const float limit = GL::Level::DirectionStepRadians(fields.at("向きの表の分割数").get<int>()) *
                        fields.at("曲げの上限の段の割合").get<float>();
    EXPECT_GT(largest, 0.0f);
    EXPECT_LE(largest, limit + 1.0e-3f);
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
