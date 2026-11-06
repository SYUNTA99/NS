#include "Editor/EditorActors.h"
#include "Game/Level/AirGravity.h"
#include "Game/Level/ImpactOutcome.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Player/ImpactEffects.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/SlamBall.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/LedgeClimbingPlayerState.h"
#include "Game/Player/States/LedgeHangingPlayerState.h"
#include "Game/Player/States/ReboundPlayerState.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Action/ActionTimeline.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/CameraModifier.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/EffectObj.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/ModelShake.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/AssetField.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/CoreObject/System/HitScreenDirector.h"
#include "Tests/TestActorFile.h"
#include "Tests/TestSceneSetup.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(std::is_base_of_v<NS::StateOf<GL::Player::IdlePlayerState, ::PlayerActor>, GL::Player::IdlePlayerState>);

namespace
{
    class ObservePlayerEffectsState final : public NS::StateOf<ObservePlayerEffectsState, ::PlayerActor>
    {
    public:
        void OnStep(::PlayerActor* player, float) override
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            EXPECT_EQ(player->OwningScene()->HitScreen().FlashFramesRemaining(), 3);
        }
    };

    PlayerActor* PlaceSequencePlayer(NS::Scene& scene)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        AddActorJson(doc, "PlayerActor", 1);
        scene.ResetFromJson(doc);
        return static_cast<PlayerActor*>(scene.Actors().FindByActorId(1));
    }

    // 上面が y = 0 の床の上 10 m の宙に居る自機を、よじ登りか落ちて着地させる
    // 着いたフレームを終えた自機を返す
    PlayerActor* LandFromTheAir(NS::Scene& scene, bool byClimb)
    {
        PlayerActor* player = PlaceSequencePlayer(scene);
        if (player == nullptr)
        {
            return nullptr;
        }
        AddTerrainBox(scene, NS::Vector3{0.0f, -0.5f, 0.0f}, 100.0f, 0.5f, 100.0f);
        player->Root().SetPosition(NS::Vector3{0.0f, 10.0f, 0.0f});
        player->Body().SetGrounded(false);
        if (byClimb)
        {
            // 縁の上面は y = 0。登り終えた体は床の上に立つ
            player->ClimbLedge();
            for (int step = 0; step < 20 && !player->States().IsCurrent<GL::Player::IdlePlayerState>(); ++step)
            {
                player->States().Step(0.125f);
            }
        }
        else
        {
            for (int frame = 0; frame < 600 && !player->Body().IsGrounded(); ++frame)
            {
                player->Update(false);
            }
        }
        if (!player->Body().IsGrounded())
        {
            return nullptr;
        }
        return player;
    }
} // namespace

TEST(PlayerStateSequence, LedgeClimbKeepsItsTwoStageTiming)
{
    NS::Scene scene;
    PlayerActor* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::Body* movement = &player->Body();
    player->Root().SetPosition(NS::Vector3{});
    player->ClimbLedge();
    ASSERT_TRUE(player->States().IsCurrent<GL::Player::LedgeClimbingPlayerState>());
    EXPECT_FLOAT_EQ(player->Root().Position().y, 0.0f);
    const float top = player->Collider().CapsuleHalfHeight() + player->Collider().CapsuleRadius();
    player->States().Step(0.0625f);
    EXPECT_NEAR(player->Root().Position().y, top * 0.5f, 0.00001f);
    player->States().Step(0.0625f);
    EXPECT_NEAR(player->Root().Position().y, top, 0.00001f);
    player->States().Step(0.125f);
    EXPECT_TRUE(player->States().IsCurrent<GL::Player::IdlePlayerState>());
    EXPECT_TRUE(movement->IsGrounded());
}

TEST(PlayerStateSequence, LeavingClimbCannotResumeAnOldPositionWrite)
{
    NS::Scene scene;
    PlayerActor* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    player->ClimbLedge();
    player->States().Step(0.0625f);
    ASSERT_TRUE(player->States().Change<GL::Player::IdlePlayerState>());
    player->Root().SetPosition(NS::Vector3{0.0f, 3.0f, 0.0f});
    player->States().Step(0.0625f);
    EXPECT_FLOAT_EQ(player->Root().Position().y, 3.0f);
}

// 着地で戻す物 (跳べる回数・コヨーテ) は、よじ登りの終わりでも普通の着地でも同じ。
// どちらも着いた次のフレームに、宙を離れて跳べる
TEST(PlayerStateSequence, ClimbEndRestoresWhatALandingRestores)
{
    for (const bool byClimb : {false, true})
    {
        SCOPED_TRACE(byClimb);
        {
            NS::Scene scene;
            PlayerActor* player = LandFromTheAir(scene, byClimb);
            ASSERT_NE(player, nullptr);
            EXPECT_EQ(player->JumpsRemaining(), 1);
        }
        {
            NS::Scene scene;
            PlayerActor* player = LandFromTheAir(scene, byClimb);
            ASSERT_NE(player, nullptr);
            player->Body().SetGrounded(false);
            player->Input().SetJumpPressed();
            player->Jump(0.0f);
            EXPECT_GT(player->Body().VerticalVelocity(), 0.0f);
        }
    }
}

// 縁から跳ぶ時も、跳べる回数・コヨーテ・先行入力を地面から跳ぶ時と同じに使い切る。
// 掴まっている間はコヨーテが減らないので、残ったままだと次の押しで宙からもう 1 度跳べる
TEST(PlayerStateSequence, LedgeJumpSpendsTheJumpLikeAGroundJump)
{
    NS::Scene scene;
    PlayerActor* player = LandFromTheAir(scene, false);
    ASSERT_NE(player, nullptr);
    player->Body().SetGrounded(false);
    player->Input().SetJumpPressed();
    ASSERT_TRUE(player->LedgeJump());
    EXPECT_EQ(player->JumpsRemaining(), 0);
    EXPECT_EQ(player->CoyoteRemaining(), 0.0f);
    player->Body().SetVerticalVelocity(0.0f);
    player->Input().SetJumpPressed();
    player->Jump(0.0f);
    EXPECT_FLOAT_EQ(player->Body().VerticalVelocity(), 0.0f);
}

namespace
{
    // 上面が y = 0、手前の面が z = 1 の縁の前に、手を上面より 0.2 m 上にして奥へ向かって落ちる自機を置く
    PlayerActor* PlaceBelowLedge(NS::Scene& scene)
    {
        PlayerActor* player = PlaceSequencePlayer(scene);
        if (player == nullptr)
        {
            return nullptr;
        }
        AddTerrainBox(scene, NS::Vector3{0.0f, -1.0f, 2.0f}, 2.0f, 1.0f, 1.0f);
        const NS::Collider& collider = player->Collider();
        player->Root().SetPosition(NS::Vector3{0.0f, 0.2f - collider.CapsuleHalfHeight(), 0.3f});
        player->Body().SetGrounded(false);
        player->Body().SetLateralVelocity(NS::Vector3{0.0f, 0.0f, 1.0f});
        return player;
    }

    // 縁の上を、上面に立った立ち姿の中心の高さより少し下から上だけ塞ぐ。
    // 上面から半分の高さの所は空いている
    void BlockAboveLedgeTop(NS::Scene& scene, const NS::Collider* collider)
    {
        NS_ASSERT(Game, collider != nullptr, "BlockAboveLedgeTop: 渡したアクターか部品が無い");
        const float bottom = collider->StandingHalfHeight() + 0.5f * collider->CapsuleRadius();
        AddTerrainBox(scene, NS::Vector3{0.0f, bottom + 1.0f, 2.0f}, 2.0f, 1.0f, 1.0f);
    }

    bool FallUntilHanging(PlayerActor* player)
    {
        NS_ASSERT(Game, player != nullptr, "FallUntilHanging: 渡したアクターか部品が無い");
        for (int frame = 0; frame < 30; ++frame)
        {
            player->Update(false);
            if (player->States().IsCurrent<GL::Player::LedgeHangingPlayerState>())
            {
                return true;
            }
        }
        return false;
    }
} // namespace

// 縁が塞がっているかは、登り終えた体の中心で確かめる。
// 立ち姿の中心が入る所が塞がっていれば、掴みもせず、掴まっていれば手を放す
TEST(PlayerStateSequence, LedgeIsRefusedWhereTheClimbEndIsBlocked)
{
    for (const bool blocked : {false, true})
    {
        SCOPED_TRACE(blocked);
        NS::Scene scene;
        PlayerActor* player = PlaceBelowLedge(scene);
        ASSERT_NE(player, nullptr);
        if (blocked)
        {
            BlockAboveLedgeTop(scene, &player->Collider());
        }
        EXPECT_EQ(FallUntilHanging(player), !blocked);
    }

    NS::Scene scene;
    PlayerActor* player = PlaceBelowLedge(scene);
    ASSERT_NE(player, nullptr);
    ASSERT_TRUE(FallUntilHanging(player));
    BlockAboveLedgeTop(scene, &player->Collider());
    player->Update(false);
    EXPECT_FALSE(player->States().IsCurrent<GL::Player::LedgeHangingPlayerState>());
}

// 立ち姿の中心より上、頭の高さまでの間に天井があると、登り終えた体が天井に入るので掴まない
TEST(PlayerStateSequence, LedgeIsRefusedUnderACeilingAboveTheClimbEnd)
{
    NS::Scene scene;
    PlayerActor* player = PlaceBelowLedge(scene);
    ASSERT_NE(player, nullptr);
    const NS::Collider& collider = player->Collider();
    const float standCenter = collider.StandingHalfHeight() + collider.CapsuleRadius();
    AddTerrainBox(scene,
                  NS::Vector3{0.0f, standCenter + 0.5f * collider.StandingHalfHeight() + 1.0f, 2.0f},
                  2.0f,
                  1.0f,
                  1.0f);
    EXPECT_FALSE(FallUntilHanging(player));
}

// 天井の端が登り終えた体の真上から横へ半径の半分だけずれていても、体の横に掛かるので掴まない
TEST(PlayerStateSequence, LedgeIsRefusedUnderACeilingThatClipsTheSideOfTheClimbEnd)
{
    NS::Scene scene;
    PlayerActor* player = PlaceBelowLedge(scene);
    ASSERT_NE(player, nullptr);
    const NS::Collider& collider = player->Collider();
    const float radius = collider.CapsuleRadius();
    const float standCenter = collider.StandingHalfHeight() + radius;
    AddTerrainBox(scene,
                  NS::Vector3{0.5f * radius + 2.0f, standCenter + 0.5f * collider.StandingHalfHeight() + 1.0f, 2.0f},
                  2.0f,
                  1.0f,
                  1.0f);
    EXPECT_FALSE(FallUntilHanging(player));
}

// 縁の上面は撃った線から測るので、手の高さと少しずれる。上面が手より少し上に出ても手を放さない
TEST(PlayerStateSequence, HangingKeepsHoldingWhenTheTopIsSlightlyAboveTheHand)
{
    NS::Scene scene;
    PlayerActor* player = PlaceBelowLedge(scene);
    ASSERT_NE(player, nullptr);
    ASSERT_TRUE(FallUntilHanging(player));
    NS::Vector3 lowered = player->Root().Position();
    lowered.y -= 2.0e-5f;
    player->Root().SetPosition(lowered);
    EXPECT_TRUE(player->HoldLedge());
    EXPECT_TRUE(player->States().IsCurrent<GL::Player::LedgeHangingPlayerState>());
}

namespace
{
    PlayerActor* PlaceBelowTurnedLedge(NS::Scene& scene, NS::OBB& outWall)
    {
        PlayerActor* player = PlaceSequencePlayer(scene);
        if (player == nullptr)
        {
            return nullptr;
        }
        const NS::Quaternion turn = NS::Quaternion::CreateFromAxisAngle(NS::Vector3{0.0f, 1.0f, 0.0f}, NS::k_Pi / 4.0f);
        outWall = NS::MakeOBB(NS::Vector3{0.0f, -1.0f, 3.0f}, turn, NS::Vector3{2.0f, 1.0f, 1.0f});
        scene.Physics().AddBox(outWall, NS::Phys::k_TerrainObjectLayer);

        const NS::Collider& collider = player->Collider();
        const NS::Vector3 faceNormal = -outWall.axisZ;
        const float away = outWall.halfExtentZ + collider.CapsuleRadius() + 0.05f;
        NS::Vector3 start = outWall.center + faceNormal * away;
        start.y = 0.2f - collider.CapsuleHalfHeight();
        player->Root().SetPosition(start);
        player->Body().SetGrounded(false);
        player->Body().SetLateralVelocity(-faceNormal);
        return player;
    }

    float DistanceToNearFace(const PlayerActor* player, const NS::OBB& wall)
    {
        const NS::Vector3 faceNormal = -wall.axisZ;
        NS::Vector3 offset = player->Root().Position() - wall.center;
        offset.y = 0.0f;
        return offset.Dot(faceNormal) - wall.halfExtentZ;
    }
} // namespace

TEST(PlayerStateSequence, TiltedSlopeIsNotALedge)
{
    NS::Scene scene;
    PlayerActor* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    const NS::Quaternion tilt = NS::Quaternion::CreateFromAxisAngle(NS::Vector3{1.0f, 0.0f, 0.0f}, -NS::k_Pi / 4.0f);
    const NS::OBB slope = NS::MakeOBB(NS::Vector3{0.0f, 0.5f, 5.0f}, tilt, NS::Vector3{3.0f, 0.5f, 5.0f});
    scene.Physics().AddBox(slope, NS::Phys::k_TerrainObjectLayer);
    const float boundingTop = slope.center.y + slope.halfExtentY * std::abs(slope.axisY.y) +
                              slope.halfExtentZ * std::abs(slope.axisZ.y);

    const NS::Collider& collider = player->Collider();
    player->Root().SetPosition(NS::Vector3{0.0f, boundingTop + 0.2f - collider.CapsuleHalfHeight(), 1.0f});
    player->Body().SetGrounded(false);
    player->Body().SetLateralVelocity(NS::Vector3{0.0f, 0.0f, 1.0f});

    EXPECT_FALSE(FallUntilHanging(player));
}

TEST(PlayerStateSequence, TurnedWallIsHeldAtItsVisibleFace)
{
    NS::Scene scene;
    NS::OBB wall{};
    PlayerActor* player = PlaceBelowTurnedLedge(scene, wall);
    ASSERT_NE(player, nullptr);
    ASSERT_TRUE(FallUntilHanging(player));
    const float radius = player->Collider().CapsuleRadius();
    EXPECT_NEAR(DistanceToNearFace(player, wall), radius, 0.05f);

    const NS::Vector3 hangPos = player->Root().Position();
    player->Input().SetClimbMove(1.0f, 0.0f);
    for (int frame = 0; frame < 20; ++frame)
    {
        player->Shimmy(1.0f / 60.0f);
    }
    NS::Vector3 moved = player->Root().Position() - hangPos;
    moved.y = 0.0f;
    EXPECT_GT(moved.Length(), 0.3f);
    EXPECT_NEAR(DistanceToNearFace(player, wall), radius, 0.05f);
}

namespace
{
    // 手前の面 z = 1 の右端 x = 2 から、angle だけ回って続く壁を足す。負の角は奥へ、正の角は手前へ折れる
    NS::OBB AddBentWall(NS::Scene& scene, float angle)
    {
        const NS::Quaternion turn = NS::Quaternion::CreateFromAxisAngle(NS::Vector3{0.0f, 1.0f, 0.0f}, angle);
        const NS::Vector3 halfExtents{2.0f, 1.0f, 1.0f};
        const NS::OBB axes = NS::MakeOBB(NS::Vector3{0.0f, 0.0f, 0.0f}, turn, halfExtents);
        const NS::Vector3 corner{2.0f, -1.0f, 1.0f};
        const NS::OBB bent =
            NS::MakeOBB(corner + axes.axisX * halfExtents.x + axes.axisZ * halfExtents.z, turn, halfExtents);
        scene.Physics().AddBox(bent, NS::Phys::k_TerrainObjectLayer);
        return bent;
    }

    // 掴まったまま 1.5 秒右へ伝い、1 フレームに動いた水平の距離の最大を返す
    float ShimmyRightAndMeasureLongestStep(PlayerActor* player)
    {
        NS_ASSERT(Game, player != nullptr, "ShimmyRightAndMeasureLongestStep: 渡したアクターが無い");
        player->Input().SetClimbMove(1.0f, 0.0f);
        float longest = 0.0f;
        for (int frame = 0; frame < 90; ++frame)
        {
            const NS::Vector3 before = player->Root().Position();
            player->Shimmy(1.0f / 60.0f);
            EXPECT_TRUE(player->HoldLedge()) << "frame " << frame;
            NS::Vector3 moved = player->Root().Position() - before;
            moved.y = 0.0f;
            longest = std::max(longest, moved.Length());
        }
        return longest;
    }

    float PastTheBend(const PlayerActor* player, const NS::OBB& bent)
    {
        NS::Vector3 pastCorner = player->Root().Position() - NS::Vector3{2.0f, -1.0f, 1.0f};
        pastCorner.y = 0.0f;
        return pastCorner.Dot(bent.axisX);
    }
} // namespace

// 奥へ折れた壁の縁を横へ伝う。掴んだ時の面の向きのまま進むと、折れた先の面から離れて止まる
TEST(PlayerStateSequence, ShimmyFollowsTheLedgeAroundAnOutwardBend)
{
    NS::Scene scene;
    PlayerActor* player = PlaceBelowLedge(scene);
    ASSERT_NE(player, nullptr);
    const NS::OBB bent = AddBentWall(scene, -NS::k_Pi / 6.0f);
    ASSERT_GT(bent.axisX.z, 0.0f);
    ASSERT_TRUE(FallUntilHanging(player));

    (void)ShimmyRightAndMeasureLongestStep(player);
    EXPECT_GT(PastTheBend(player, bent), 0.7f);
    EXPECT_NEAR(DistanceToNearFace(player, bent), player->Collider().CapsuleRadius(), 0.05f);
}

// 手前へ折れた壁の縁を横へ伝う。折れた先の面の前へ置き直すと、角で面に沿って後ろへ跳ぶ
TEST(PlayerStateSequence, ShimmyAroundAnInwardBendDoesNotJump)
{
    NS::Scene scene;
    PlayerActor* player = PlaceBelowLedge(scene);
    ASSERT_NE(player, nullptr);
    const NS::OBB bent = AddBentWall(scene, NS::k_Pi / 6.0f);
    ASSERT_LT(bent.axisX.z, 0.0f);
    ASSERT_TRUE(FallUntilHanging(player));

    // 横移動は 1 フレームに数 cm。面の前へ置き直すと、角で半径の半分 (約 0.3 m) 跳ぶ
    EXPECT_LT(ShimmyRightAndMeasureLongestStep(player), 0.1f);
    EXPECT_GT(PastTheBend(player, bent), 0.7f);
    EXPECT_NEAR(DistanceToNearFace(player, bent), player->Collider().CapsuleRadius(), 0.05f);
}

TEST(PlayerStateSequence, OnlyActorUpdateAdvancesTheOwnedStateMachine)
{
    NS::Scene scene;
    PlayerActor* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    player->Body().SetGrounded(true);
    ASSERT_TRUE(player->States().IsCurrent<GL::Player::IdlePlayerState>());
    // 置いただけでは先頭の立ちへ入る予約のままで、入って 1 歩目を進めるのは自機の Update
    EXPECT_FALSE(player->States().IsFirstStep());
    player->Body().SetGrounded(true);
    player->Update();
    EXPECT_TRUE(player->States().IsCurrent<GL::Player::IdlePlayerState>());
    EXPECT_EQ(player->States().StepCount(), 1u);
}

TEST(PlayerStateSequence, EffectsAdvanceAfterTheActorStateStep)
{
    NS::Scene scene;
    PlayerActor* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::HitScreenDirector* screen = &scene.HitScreen();
    ASSERT_NE(screen, nullptr);
    screen->StartFlash(player, 3, 1.0f);
    screen->OnTick();
    ASSERT_EQ(screen->FlashFramesRemaining(), 3);
    player->States().Build<ObservePlayerEffectsState>(player);
    // HitScreenDirector はアクターの PrepareRender の後に進む。自機の状態の歩は、薄れる前の白を見る
    scene.OnUpdate();
    EXPECT_EQ(player->States().StepCount(), 1u);
    EXPECT_EQ(screen->FlashFramesRemaining(), 2);
}

TEST(PlayerStateSequence, ReboundKeepsGravityOrderAndIgnoresJump)
{
    NS::Scene scene;
    PlayerActor* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::Body* movement = &player->Body();
    movement->SetGrounded(true);
    const GL::Player::ReboundArc arc{.apexHeight = 1.0f, .distance = 5.0f};
    const NS::Vector3 initial = player->ReboundVelocityFor(arc);
    ASSERT_TRUE(player->BeginRebound(arc));
    EXPECT_FLOAT_EQ(movement->VerticalVelocity(), initial.y);
    player->Input().SetJumpPressed();
    player->States().Step(0.01f);
    EXPECT_NEAR(movement->VerticalVelocity(), initial.y - 0.125f, 0.00001f);
    EXPECT_TRUE(player->States().IsCurrent<GL::Player::ReboundPlayerState>());
    player->States().Step(0.02f);
    EXPECT_NEAR(movement->VerticalVelocity(), initial.y - 0.375f, 0.00001f);
    movement->SetVerticalVelocity(0.0f);
    player->States().Step(0.01f);
    EXPECT_TRUE(player->States().IsCurrent<GL::Player::IdlePlayerState>());
}

// 反動の 1 フレームに当てる重力は、普段と同じ選び方 ChooseGravity へ反動の組 (上りだけ倍率付き) を渡した値
// 頂点の帯 (縦の速さの大きさが頂点滞空 Vy 未満) を反動でも同じ式で見るので、1 と -1 の場合が帯を外すと割れる
TEST(PlayerStateSequence, ReboundGravityIsTheChoiceWithTheReboundRise)
{
    NS::Scene scene;
    PlayerActor* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(NS::ApplyJsonFields(&player->Params(), {{"頂点滞空 Vy", 3.0f}, {"頂点滞空倍率", 0.3f}}), 0u);
    constexpr float k_Dt = 1.0f / 60.0f;
    const GL::Level::AirGravity gravity = player->Params().ReboundGravity();
    for (const float vertical : {5.0f, 1.0f, -1.0f, -5.0f})
    {
        SCOPED_TRACE(vertical);
        player->Body().SetVerticalVelocity(vertical);
        player->ReboundGravity(k_Dt);
        EXPECT_FLOAT_EQ(player->Body().VerticalVelocity(),
                        vertical + GL::Level::ChooseGravity(gravity, vertical) * k_Dt);
    }
}

// 突進の玉は、丸まる前の立ち姿の下の球がそのまま玉になる。丸まる時の根の下げ幅 (ChangeCurled)
// と玉の中心の決まりが結ばれている 下げ幅を変えると、判定・矢印・エディタの面が読む玉だけが黙ってずれる
TEST(PlayerStateSequence, SlamBallStaysPutWhenCurling)
{
    NS::Scene scene;
    PlayerActor* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    player->Root().SetPosition(NS::Vector3{1.0f, 2.0f, 3.0f});
    ASSERT_FALSE(player->IsCurled());
    const NS::Vector3 standingRoot = player->Root().Position();
    const NS::Sphere standing = GL::Player::SlamBallAt(&player->Collider(), standingRoot);
    EXPECT_FLOAT_EQ(standing.center.y, standingRoot.y - player->Collider().StandingHalfHeight());
    EXPECT_FLOAT_EQ(standing.radius, player->Collider().CapsuleRadius());

    player->SetCurled(true);
    ASSERT_TRUE(player->IsCurled());
    const NS::Sphere curled = GL::Player::SlamBallAt(&player->Collider(), player->Root().Position());
    EXPECT_FLOAT_EQ(curled.center.x, standing.center.x);
    EXPECT_FLOAT_EQ(curled.center.y, standing.center.y);
    EXPECT_FLOAT_EQ(curled.center.z, standing.center.z);
    EXPECT_FLOAT_EQ(curled.radius, standing.radius);
}

// 丸まりの正は当たりの形。丸まる・解く・やり直しの直後に、丸まりの答え・当たりの形・描くメッシュが揃う
TEST(PlayerStateSequence, CurledAnswerShapeAndDrawnMeshAgree)
{
    NS::Scene scene;
    PlayerActor* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    const NS::Model* model = player->ModelClassObj();
    ASSERT_NE(model, nullptr);
    const nlohmann::json looks = NS::SerializeClassObjFields(&player->Appearance());
    NS::Mesh* standingMesh = nullptr;
    NS::Mesh* ballMesh = nullptr;
    NS::LoadAssetField(standingMesh, looks["立ち姿のメッシュ"].get<std::string>());
    NS::LoadAssetField(ballMesh, looks["玉のメッシュ"].get<std::string>());
    ASSERT_NE(standingMesh, ballMesh);

    const auto expectAgree = [&](bool curled, const char* when)
    {
        EXPECT_EQ(player->IsCurled(), curled) << when;
        EXPECT_EQ(player->Collider().CapsuleHalfHeight() == 0.0f, curled) << when;
        const NS::Mesh* expected = standingMesh;
        if (curled)
        {
            expected = ballMesh;
        }
        EXPECT_EQ(model->GetMesh(), expected) << when;
    };

    expectAgree(false, "置いた直後");
    player->SetCurled(true);
    expectAgree(true, "丸まった直後");
    player->SetCurled(false);
    expectAgree(false, "解いた直後");
    player->SetCurled(true);
    player->ResetState();
    expectAgree(false, "やり直しの直後");
}

TEST(PlayerStateSequence, EndPlayCancelsTheClimbBeforePartsLeave)
{
    NS::Scene scene;
    PlayerActor* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    player->ClimbLedge();
    player->States().Step(0.0625f);
    player->OnEndPlay();
    player->Root().SetPosition(NS::Vector3{0.0f, 3.0f, 0.0f});
    player->States().Step(0.0625f);
    EXPECT_FLOAT_EQ(player->Root().Position().y, 3.0f);
}

TEST(PlayerStateSequence, TheBaseOwnsTheMachineFromInit)
{
    const std::unique_ptr<PlayerActor> playerOwned = NS::CreateActor<PlayerActor>();
    PlayerActor& player = *playerOwned;
    ASSERT_NE(player.GetStateMachine(), nullptr);
    EXPECT_EQ(player.GetStateMachine()->CurrentId(), NS::StateIdOf<GL::Player::IdlePlayerState>());
    EXPECT_TRUE(NS::IsState<GL::Player::IdlePlayerState>(&player));
}
