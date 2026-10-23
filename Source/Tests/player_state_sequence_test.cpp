#include "Editor/EditorActors.h"
#include "Game/Level/AirGravity.h"
#include "Game/Level/ImpactOutcome.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/PropActor.h"
#include "Game/Player/ImpactEffects.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/LedgeClimbingPlayerState.h"
#include "Game/Player/States/LedgeHangingPlayerState.h"
#include "Game/Player/States/ReboundPlayerState.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ClassObjs/ActionTimeline.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/CameraModifier.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/EffectKeeper.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/ModelShake.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IUse/IUseSceneSystem.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/AssetField.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/CoreObject/System/HitScreenDirector.h"
#include "NSlib/CoreObject/System/SceneSystem.h"
#include "NSlib/CoreObject/UpdatePhase.h"
#include "Tests/TestActorFile.h"
#include "Tests/TestTackleScene.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

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
        void OnStep(::PlayerActor& player, float) override
        {
            EXPECT_EQ(NS::FindSceneSystem<NS::HitScreenDirector>(&player)->FlashFramesRemaining(), 3);
        }
    };

    PlayerActor* PlaceSequencePlayer(NS::Scene& scene)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json entry = NS::MakeActorJson();
        NS::SetActorJsonFile(entry, "PlayerActor");
        NS::SetActorJsonId(entry, 1);
        NS::SceneJsonActors(doc).push_back(std::move(entry));
        scene.ResetFromJson(doc);
        return static_cast<PlayerActor*>(scene.Actors().FindByActorId(1));
    }

    // 上面が y = 0 の床の上 10 m で、宙で突進を出して空中の 1 発を使い切った自機を、よじ登りか落ちて着地させる
    // 着いたフレームを終えた自機を返す
    PlayerActor* LandAfterAirSlam(NS::Scene& scene, bool byClimb)
    {
        PlayerActor* player = PlaceSequencePlayer(scene);
        if (player == nullptr)
        {
            return nullptr;
        }
        NS::OBB floor{};
        floor.center = NS::Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 100.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 100.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
        player->Root().SetPosition(NS::Vector3{0.0f, 10.0f, 0.0f});
        player->Body().SetGrounded(false);
        player->RequestBodySlam(0.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
        if (!player->BodySlam())
        {
            return nullptr;
        }
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

// 着地で戻す物 (跳べる回数・コヨーテ・空中の突進の使い切り) は、よじ登りの終わりでも普通の着地でも同じ。
// どちらも着いた次のフレームに、宙を離れて跳べ、突進の頼みで突進が出る
TEST(PlayerStateSequence, ClimbEndRestoresWhatALandingRestores)
{
    for (const bool byClimb : {false, true})
    {
        SCOPED_TRACE(byClimb);
        {
            NS::Scene scene;
            PlayerActor* player = LandAfterAirSlam(scene, byClimb);
            ASSERT_NE(player, nullptr);
            EXPECT_EQ(player->JumpsRemaining(), 1);
            player->RequestBodySlam(0.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
            player->Update(false);
            EXPECT_TRUE(player->IsBodySlamming());
        }
        {
            NS::Scene scene;
            PlayerActor* player = LandAfterAirSlam(scene, byClimb);
            ASSERT_NE(player, nullptr);
            player->Body().SetGrounded(false);
            player->SetJumpPressed();
            player->Jump(0.0f);
            EXPECT_GT(player->Body().VerticalVelocity(), 0.0f);
        }
    }
}

// 通常突進の途中で押し直し、溜めのしきい値を越えても、突進は欄「通常突進の距離」を進み切るまで続く。
// 溜め始めのフレームに水平を 0 にすると、次の移動で進めずに突進が打ち切られる
TEST(PlayerStateSequence, ChargingDuringATapSlamKeepsTheSlamGoing)
{
    NS::Scene scene;
    PlayerActor* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    constexpr float k_TapDistance = 6.0f;
    ASSERT_EQ(NS::ApplyJsonFields(player->Params(), {{"通常突進の距離", k_TapDistance}}), 0u);
    player->Root().SetPosition(NS::Vector3{0.0f, 10.0f, 0.0f});
    player->Body().SetGrounded(false);
    player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    player->Update(true);
    const NS::Vector3 start = player->Root().Position();
    player->Update(false);
    ASSERT_TRUE(player->IsBodySlamming());
    for (int frame = 0; frame < 120 && player->IsBodySlamming(); ++frame)
    {
        player->Update(true);
    }
    ASSERT_FALSE(player->IsBodySlamming());
    const NS::Vector3 end = player->Root().Position();
    const float dx = end.x - start.x;
    const float dz = end.z - start.z;
    EXPECT_GE(std::sqrt(dx * dx + dz * dz), k_TapDistance - 0.001f);
}

// 縁から跳ぶ時も、跳べる回数・コヨーテ・先行入力を地面から跳ぶ時と同じに使い切る。
// 掴まっている間はコヨーテが減らないので、残ったままだと次の押しで宙からもう 1 度跳べる
TEST(PlayerStateSequence, LedgeJumpSpendsTheJumpLikeAGroundJump)
{
    NS::Scene scene;
    PlayerActor* player = LandAfterAirSlam(scene, false);
    ASSERT_NE(player, nullptr);
    player->Body().SetGrounded(false);
    player->SetJumpPressed();
    ASSERT_TRUE(player->LedgeJump());
    EXPECT_EQ(player->JumpsRemaining(), 0);
    EXPECT_EQ(player->CoyoteRemaining(), 0.0f);
    player->Body().SetVerticalVelocity(0.0f);
    player->SetJumpPressed();
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
        NS::OBB ledge{};
        ledge.center = NS::Vector3{0.0f, -1.0f, 2.0f};
        ledge.halfExtentX = 2.0f;
        ledge.halfExtentY = 1.0f;
        ledge.halfExtentZ = 1.0f;
        scene.Physics().AddBox(ledge, NS::Phys::ObjectLayers::Terrain);
        const NS::Collider& collider = player->Collider();
        player->Root().SetPosition(NS::Vector3{0.0f, 0.2f - collider.CapsuleHalfHeight(), 0.3f});
        player->Body().SetGrounded(false);
        player->Body().SetLateralVelocity(NS::Vector3{0.0f, 0.0f, 1.0f});
        return player;
    }

    // 縁の上を、上面に立った立ち姿の中心の高さより少し下から上だけ塞ぐ。
    // 上面から半分の高さの所は空いている
    void BlockAboveLedgeTop(NS::Scene& scene, const NS::Collider& collider)
    {
        const float bottom = collider.StandingHalfHeight() + 0.5f * collider.CapsuleRadius();
        NS::OBB blocker{};
        blocker.center = NS::Vector3{0.0f, bottom + 1.0f, 2.0f};
        blocker.halfExtentX = 2.0f;
        blocker.halfExtentY = 1.0f;
        blocker.halfExtentZ = 1.0f;
        scene.Physics().AddBox(blocker, NS::Phys::ObjectLayers::Terrain);
    }

    bool FallUntilHanging(PlayerActor& player)
    {
        for (int frame = 0; frame < 30; ++frame)
        {
            player.Update(false);
            if (player.States().IsCurrent<GL::Player::LedgeHangingPlayerState>())
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
            BlockAboveLedgeTop(scene, player->Collider());
        }
        EXPECT_EQ(FallUntilHanging(*player), !blocked);
    }

    NS::Scene scene;
    PlayerActor* player = PlaceBelowLedge(scene);
    ASSERT_NE(player, nullptr);
    ASSERT_TRUE(FallUntilHanging(*player));
    BlockAboveLedgeTop(scene, player->Collider());
    player->Update(false);
    EXPECT_FALSE(player->States().IsCurrent<GL::Player::LedgeHangingPlayerState>());
}

TEST(PlayerStateSequence, OnlyActorUpdateAdvancesTheOwnedStateMachine)
{
    NS::Scene scene;
    PlayerActor* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    player->Body().SetGrounded(true);
    ASSERT_TRUE(player->States().IsCurrent<GL::Player::IdlePlayerState>());
    EXPECT_EQ(player->States().StepsInState(), 0u);
    player->Body().SetGrounded(true);
    player->Update();
    EXPECT_EQ(player->States().StepsInState(), 1u);
}

TEST(PlayerStateSequence, EffectsAdvanceAfterTheActorStateStep)
{
    NS::Scene scene;
    PlayerActor* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::HitScreenDirector* screen = NS::FindSceneSystem<NS::HitScreenDirector>(player);
    ASSERT_NE(screen, nullptr);
    screen->StartFlash(*player, 3, 1.0f);
    screen->OnTick();
    ASSERT_EQ(screen->FlashFramesRemaining(), 3);
    player->States().Build<ObservePlayerEffectsState>(*player);
    // HitScreenDirector は描く支度の段で進む。自機の状態の歩は、薄れる前の白を見る
    scene.OnUpdate();
    EXPECT_EQ(player->States().StepsInState(), 1u);
    EXPECT_EQ(screen->FlashFramesRemaining(), 2);
}

// 止めは本物の当たりで作る。止めの間は状態の歩が進まず、当たりの演出は薄れていく
TEST(PlayerStateSequence, FrozenMovementDoesNotFreezeEffects)
{
    nlohmann::json doc = NS::MakeSceneJson();
    nlohmann::json entry = NS::MakeActorJson();
    NS::SetActorJsonFile(entry, "PlayerActor");
    NS::SetActorJsonId(entry, 1);
    NS::Editor::SetActorJsonPosition(entry, NS::Vector3{0.0f, 1.0f, 0.0f});
    NS::SceneJsonActors(doc).push_back(std::move(entry));
    nlohmann::json rock = NS::MakeActorJson();
    NS::SetActorJsonFile(rock, "PropActor");
    NS::SetActorJsonId(rock, 2);
    NS::Editor::SetActorJsonPosition(rock, NS::Vector3{0.0f, 0.5f, 0.6f});
    NS::SceneJsonActors(doc).push_back(std::move(rock));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    NS::OBB floor{};
    floor.center = NS::Vector3{0.0f, -0.5f, 0.0f};
    floor.halfExtentX = 100.0f;
    floor.halfExtentY = 0.5f;
    floor.halfExtentZ = 100.0f;
    scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
    PlayerActor* player = static_cast<PlayerActor*>(scene.Actors().FindByActorId(1));
    ASSERT_NE(player, nullptr);
    player->RequestBodySlam(1.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    for (int frame = 0; frame < 10 && !player->IsInTackleHit(); ++frame)
    {
        player->Update();
    }
    ASSERT_TRUE(player->IsInTackleHit());
    NS::HitScreenDirector* screen = NS::FindSceneSystem<NS::HitScreenDirector>(player);
    ASSERT_NE(screen, nullptr);
    screen->StartFlash(*player, 3, 1.0f);
    screen->OnTick();
    const NS::uint32 stateStep = player->States().StepsInState();
    scene.OnUpdate();
    ASSERT_TRUE(player->IsHitStopping());
    EXPECT_EQ(player->States().StepsInState(), stateStep);
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
    player->SetJumpPressed();
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
    ASSERT_EQ(NS::ApplyJsonFields(player->Params(), {{"頂点滞空 Vy", 3.0f}, {"頂点滞空倍率", 0.3f}}), 0u);
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
    const NS::Sphere standing = player->SlamBallAt(standingRoot);
    EXPECT_FLOAT_EQ(standing.center.y, standingRoot.y - player->Collider().StandingHalfHeight());
    EXPECT_FLOAT_EQ(standing.radius, player->Collider().CapsuleRadius());

    player->SetCurled(true);
    ASSERT_TRUE(player->IsCurled());
    const NS::Sphere curled = player->SlamBallAt(player->Root().Position());
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
    const nlohmann::json looks = NS::SerializeClassObjFields(player->Appearance());
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

TEST(PlayerStateSequence, ReboundAndBodySlamAreNeverBothTrue)
{
    NS::Scene scene;
    PlayerActor* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::Body& movement = player->Body();
    movement.SetGrounded(true);
    ASSERT_TRUE(player->BeginRebound(GL::Player::ReboundArc{.apexHeight = 1.0f, .distance = 5.0f}));
    EXPECT_TRUE(player->IsRebounding());
    EXPECT_FALSE(player->IsBodySlamming());

    player->RequestBodySlam(0.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    EXPECT_TRUE(player->IsBodySlamming());
    EXPECT_FALSE(player->IsRebounding());

    ASSERT_TRUE(player->BeginRebound(GL::Player::ReboundArc{.apexHeight = 1.0f, .distance = 5.0f}));
    EXPECT_TRUE(player->IsRebounding());
    EXPECT_FALSE(player->IsBodySlamming());
}

// 当たりから明けと反動までを、自機の状態の歩と基底の止めで縛る。横軸は検知の歩を 0 にした歩

namespace
{
    // 検知の歩を 0 にした 1 歩の自機と置物とカメラ
    struct TackleStep
    {
        bool canMoveBody = true;
        bool inTackleHit = false;
        bool rebounding = false;
        bool rockFrozen = false;
        bool rockFlying = false;
        NS::Vector3 root{};
        float worldSpeed = 1.0f;
        bool sinkShown = false;
        float sinkPixels = 0.0f;
    };

    TackleStep ReadTackleStep(NS::Scene& scene, PlayerActor& player)
    {
        TackleStep step;
        step.canMoveBody = player.CanMoveBody();
        step.inTackleHit = player.IsInTackleHit();
        step.rebounding = player.IsRebounding();
        if (const GL::Level::PropActor* rock = TackleRockOf(scene))
        {
            step.rockFrozen = rock->IsFrozen();
            step.rockFlying = rock->IsFlying();
        }
        step.root = player.Root().Position();
        step.worldSpeed = scene.WorldSpeed();
        if (const NS::CameraManager* cameras = player.GetCameraManager())
        {
            if (const NS::CameraSinkModifier* sink = cameras->FindModifier<NS::CameraSinkModifier>())
            {
                step.sinkShown = true;
                step.sinkPixels = sink->Pixels();
            }
        }
        return step;
    }

    // +z へ突進を出し、全部の段を回して、検知の歩から frames 歩の姿を並べる。当たらなければ空
    // each は歩 k を読んだ後に k を渡して呼ぶ
    std::vector<TackleStep> RunTackle(NS::Scene& scene,
                                      PlayerActor& player,
                                      float charge01,
                                      int frames,
                                      float overcharge01 = 0.0f,
                                      const std::function<void(int)>& each = {})
    {
        std::vector<TackleStep> trace;
        const NS::uint32 before = player.Resolver().Answer().lastImpact.sequence;
        player.RequestBodySlam(charge01, NS::Vector3{0.0f, 0.0f, 1.0f}, 0.0f, overcharge01);
        for (int frame = 0; frame < 120 + frames && static_cast<int>(trace.size()) < frames; ++frame)
        {
            scene.OnUpdate();
            if (trace.empty() && player.Resolver().Answer().lastImpact.sequence == before)
            {
                continue;
            }
            trace.push_back(ReadTackleStep(scene, player));
            if (each)
            {
                each(static_cast<int>(trace.size()) - 1);
            }
        }
        return trace;
    }

    // 自機の .actor の束 name の止めの長さを、試しの間だけ stopFrames にする。シーンに置く前に作る
    class ScopedPlayerSetStop
    {
    public:
        ScopedPlayerSetStop(std::string_view name, int stopFrames)
        {
            NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
            const nlohmann::json* shipped = library.Find("PlayerActor");
            EXPECT_NE(shipped, nullptr);
            if (shipped == nullptr)
            {
                return;
            }
            m_original = *shipped;
            nlohmann::json edited = m_original;
            bool found = false;
            for (nlohmann::json& set : edited["hitReactions"])
            {
                if (set.value("name", std::string{}) == name)
                {
                    set["stopFrames"] = stopFrames;
                    found = true;
                }
            }
            EXPECT_TRUE(found) << name;
            m_replaced = library.Set("PlayerActor", std::move(edited));
            EXPECT_TRUE(m_replaced);
        }

        ~ScopedPlayerSetStop()
        {
            if (m_replaced)
            {
                (void)NS::ActorFileLibrary::Get().Set("PlayerActor", std::move(m_original));
            }
        }

        ScopedPlayerSetStop(const ScopedPlayerSetStop&) = delete;
        ScopedPlayerSetStop& operator=(const ScopedPlayerSetStop&) = delete;

    private:
        nlohmann::json m_original;
        bool m_replaced = false;
    };

    // 自機の体が動けなかった歩の数
    int CountHeldSteps(const std::vector<TackleStep>& trace)
    {
        int count = 0;
        for (const TackleStep& step : trace)
        {
            if (!step.canMoveBody)
            {
                ++count;
            }
        }
        return count;
    }

    // 壊れる動きを持つと答え、送られた止めの上書きを控える置物。今の置物は壊れないと答える
    class BreakableTackleRock final : public GL::Level::PropActor
    {
    public:
        int stopOverride = -1;

    protected:
        bool ReceiveMsg(const NS::Message& msg, NS::HitSensor* sender, NS::HitSensor* receiver) override
        {
            if (const GL::Level::MsgTackleHit* hit = NS::MsgCast<GL::Level::MsgTackleHit>(msg))
            {
                stopOverride = hit->Desc().stopOverride.value_or(-1);
            }
            const bool answered = GL::Level::PropActor::ReceiveMsg(msg, sender, receiver);
            if (const GL::Level::MsgAskTackleTarget* ask = NS::MsgCast<GL::Level::MsgAskTackleTarget>(msg))
            {
                ask->Answer().breakable = true;
            }
            return answered;
        }
    };
} // namespace

// 真ん中は 1〜12 歩目、外れは 1〜4 歩目に、自機は当たりの状態で状態も体も止まる。
// 止めが明けた歩 (真ん中 13・外れ 5) に反動へ移り、置物へ明けが届いて飛ぶ
TEST(PlayerTackleHit, TheHitHoldsTheStateAndBodyUntilTheReleaseStep)
{
    struct HitCase
    {
        float rockX;
        GL::Level::HitTier tier;
        int stop;
    };
    for (const HitCase& hit :
         {HitCase{0.0f, GL::Level::HitTier::Center, 12}, HitCase{0.75f, GL::Level::HitTier::Wide, 4}})
    {
        SCOPED_TRACE(hit.stop);
        NS::Scene scene;
        PlayerActor* player = PlaceTackleScene(scene, hit.rockX);
        ASSERT_NE(player, nullptr);
        const std::vector<TackleStep> trace = RunTackle(scene, *player, 1.0f, hit.stop + 3);
        ASSERT_EQ(trace.size(), static_cast<std::size_t>(hit.stop + 3));
        ASSERT_EQ(player->Resolver().Answer().lastImpact.tier, hit.tier);
        EXPECT_TRUE(trace[0].canMoveBody);
        EXPECT_FALSE(trace[0].inTackleHit);
        for (int k = 1; k <= hit.stop; ++k)
        {
            SCOPED_TRACE(k);
            const TackleStep& step = trace[static_cast<std::size_t>(k)];
            EXPECT_FALSE(step.canMoveBody);
            EXPECT_TRUE(step.inTackleHit);
            EXPECT_FALSE(step.rebounding);
            EXPECT_FLOAT_EQ(step.root.x, trace[0].root.x);
            EXPECT_FLOAT_EQ(step.root.y, trace[0].root.y);
            EXPECT_FLOAT_EQ(step.root.z, trace[0].root.z);
            EXPECT_TRUE(step.rockFrozen);
            EXPECT_FALSE(step.rockFlying);
        }
        const TackleStep& release = trace[static_cast<std::size_t>(hit.stop + 1)];
        EXPECT_TRUE(release.canMoveBody);
        EXPECT_FALSE(release.inTackleHit);
        EXPECT_TRUE(release.rebounding);
        EXPECT_TRUE(release.rockFlying);
        EXPECT_EQ(player->Resolver().Answer().lastImpact.hitStopSteps, hit.stop);
    }
}

// 止めの長さを決めるのは段の束の止めの長さだけ。12 を 8 にすると止めが 8 になり、9 歩目に明ける
TEST(PlayerTackleHit, TheStopIsAsLongAsTheSetSays)
{
    for (const int stop : {12, 8})
    {
        SCOPED_TRACE(stop);
        const ScopedPlayerSetStop scoped("体当たり[真ん中]", stop);
        NS::Scene scene;
        PlayerActor* player = PlaceTackleScene(scene, 0.0f);
        ASSERT_NE(player, nullptr);
        const std::vector<TackleStep> trace = RunTackle(scene, *player, 1.0f, stop + 4);
        ASSERT_EQ(trace.size(), static_cast<std::size_t>(stop + 4));
        EXPECT_EQ(CountHeldSteps(trace), stop);
        EXPECT_TRUE(trace[static_cast<std::size_t>(stop + 1)].rebounding);
        EXPECT_EQ(player->Resolver().Answer().lastImpact.hitStopSteps, stop);
        EXPECT_TRUE(player->Resolver().Answer().lastImpact.localStop);
    }
}

// 止めの長さは溜めと相手の質量で変わらない
TEST(PlayerTackleHit, TheStopIgnoresChargeAndMass)
{
    for (const float charge : {0.0f, 1.0f})
    {
        for (const float mass : {0.5f, 8.0f})
        {
            SCOPED_TRACE(charge);
            SCOPED_TRACE(mass);
            NS::Scene scene;
            PlayerActor* player = PlaceTackleScene(scene, 0.0f, 0.6f, mass);
            ASSERT_NE(player, nullptr);
            const std::vector<TackleStep> trace = RunTackle(scene, *player, charge, 16);
            ASSERT_EQ(trace.size(), 16u);
            ASSERT_EQ(player->Resolver().Answer().lastImpact.tier, GL::Level::HitTier::Center);
            EXPECT_EQ(CountHeldSteps(trace), 12);
        }
    }
}

// 止めの長さが 0 の束の当たりは、止めを使った事を記録に残さない
TEST(PlayerTackleHit, TheRecordNamesOnlyTheStopsTheSetUsed)
{
    const ScopedPlayerSetStop scoped("体当たり[真ん中]", 0);
    NS::Scene scene;
    PlayerActor* player = PlaceTackleScene(scene, 0.0f);
    ASSERT_NE(player, nullptr);
    const std::vector<TackleStep> trace = RunTackle(scene, *player, 1.0f, 4);
    ASSERT_EQ(trace.size(), 4u);
    EXPECT_EQ(player->Resolver().Answer().lastImpact.hitStopSteps, 0);
    EXPECT_FALSE(player->Resolver().Answer().lastImpact.localStop);
    EXPECT_FALSE(player->Resolver().Answer().lastImpact.gradualRelease);
    EXPECT_EQ(CountHeldSteps(trace), 1) << "止めの無い束でも、入った歩は当たりの状態に居る";
}

// 紫の溜めの真ん中だけ、止めが明けた歩から世界が遅くなる。赤の溜めきりと外れは遅くしない。
// やり直しとプレイの終わりで、遅い世界を持ち越さない
TEST(PlayerTackleHit, OnlyAPurpleCenterSlowsTheWorldFromTheRelease)
{
    struct SlowCase
    {
        float rockX;
        float overcharge;
        bool slows;
        int release;
    };
    for (const bool endPlay : {false, true})
    {
        for (const SlowCase& each :
             {SlowCase{0.0f, 0.0f, false, 13}, SlowCase{0.0f, 0.4f, true, 13}, SlowCase{0.75f, 0.4f, false, 5}})
        {
            SCOPED_TRACE(endPlay);
            SCOPED_TRACE(each.rockX);
            SCOPED_TRACE(each.overcharge);
            NS::Scene scene;
            PlayerActor* player = PlaceTackleScene(scene, each.rockX);
            ASSERT_NE(player, nullptr);
            const std::vector<TackleStep> trace = RunTackle(scene, *player, 1.0f, each.release + 1, each.overcharge);
            ASSERT_EQ(trace.size(), static_cast<std::size_t>(each.release + 1));
            EXPECT_FLOAT_EQ(trace[static_cast<std::size_t>(each.release - 1)].worldSpeed, 1.0f);
            if (each.slows)
            {
                EXPECT_LT(trace[static_cast<std::size_t>(each.release)].worldSpeed, 1.0f);
            }
            else
            {
                EXPECT_FLOAT_EQ(trace[static_cast<std::size_t>(each.release)].worldSpeed, 1.0f);
            }
            EXPECT_EQ(player->Resolver().Answer().lastImpact.gradualRelease, each.slows);
            if (endPlay)
            {
                player->OnEndPlay();
            }
            else
            {
                player->ResetState();
            }
            EXPECT_FLOAT_EQ(scene.WorldSpeed(), 1.0f);
        }
    }
}

// 真ん中のカメラの沈みは、止めの間は底でこらえ、反動の状態に入った歩に始めた束「反動」で跳ね返る
TEST(PlayerTackleHit, TheReboundSetBouncesTheSinkFromTheReboundStep)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTackleScene(scene, 0.0f);
    ASSERT_NE(player, nullptr);
    const std::vector<TackleStep> trace = RunTackle(scene, *player, 1.0f, 16);
    ASSERT_EQ(trace.size(), 16u);
    EXPECT_FALSE(trace[0].sinkShown);
    ASSERT_TRUE(trace[1].sinkShown);
    EXPECT_LT(trace[1].sinkPixels, 0.0f);
    const float bottom = trace[9].sinkPixels;
    EXPECT_LT(bottom, 0.0f);
    for (int k = 9; k <= 13; ++k)
    {
        SCOPED_TRACE(k);
        EXPECT_NEAR(trace[static_cast<std::size_t>(k)].sinkPixels, bottom, 1.0e-4f);
    }
    EXPECT_TRUE(trace[13].rebounding);
    EXPECT_GT(trace[14].sinkPixels, bottom + 0.1f);
}

// 止めの途中のやり直しでは反動が来ないので、沈みは跳ね返らず、沈みの長さの後に残らない。
// やり直しの後は止めも当たりの状態も残らない
TEST(PlayerTackleHit, AResetDuringTheStopLeavesTheSinkWithoutABounce)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTackleScene(scene, 0.0f);
    ASSERT_NE(player, nullptr);
    const std::vector<TackleStep> trace = RunTackle(scene, *player, 1.0f, 40, 0.0f, [&](int k) {
        if (k == 5)
        {
            ASSERT_TRUE(player->IsInTackleHit());
            player->ResetState();
            EXPECT_FALSE(player->IsInTackleHit());
            EXPECT_FALSE(player->IsHitStopping());
            EXPECT_TRUE(player->CanMoveBody());
        }
    });
    ASSERT_EQ(trace.size(), 40u);
    for (int k = 6; k < 40; ++k)
    {
        SCOPED_TRACE(k);
        const TackleStep& step = trace[static_cast<std::size_t>(k)];
        EXPECT_FALSE(step.rebounding);
        EXPECT_LE(step.sinkPixels, 0.0f) << "跳ね返りは 0 を越えて上へ出る";
    }
    EXPECT_FALSE(trace[34].sinkShown);
}

// 止めが明けた後のやり直しは、前の当たりの沈みの薄れを残す
TEST(PlayerTackleHit, AResetAfterTheReleaseKeepsTheFadingReturns)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTackleScene(scene, 0.0f);
    ASSERT_NE(player, nullptr);
    const std::vector<TackleStep> trace = RunTackle(scene, *player, 1.0f, 16, 0.0f, [&](int k) {
        if (k == 15)
        {
            ASSERT_TRUE(player->IsRebounding());
            player->ResetState();
            const NS::CameraManager* cameras = player->GetCameraManager();
            ASSERT_NE(cameras, nullptr);
            EXPECT_NE(cameras->FindModifier<NS::CameraSinkModifier>(), nullptr);
        }
    });
    ASSERT_EQ(trace.size(), 16u);
}

// やり直しは、触れる前の自機以外の止めも解く
TEST(PlayerTackleHit, AResetReleasesTheOthersStop)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTackleScene(scene, 0.0f);
    ASSERT_NE(player, nullptr);
    player->RequestBodySlam(1.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
    bool held = false;
    for (int frame = 0; frame < 60 && !held; ++frame)
    {
        scene.OnUpdate();
        held = scene.IsHoldingOthers();
    }
    ASSERT_TRUE(held);
    player->ResetState();
    EXPECT_FALSE(scene.IsHoldingOthers());
}

// 貫通の当たりは、束を始めた後に止めの残りを欄「貫通の止め秒」の長さで上書きし、置物へも同じ長さを送る。
// 明けで反動せずに当たりの状態を出る
TEST(PlayerTackleHit, ABreakOverridesTheStopWithTheBreakStop)
{
    NS::Scene scene;
    // 置いた岩は線の外へ退け、壊れる岩で見る
    PlayerActor* player = PlaceTackleScene(scene, 50.0f);
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(
        NS::ApplyJsonFields(player->Params(), nlohmann::json{{"破壊を許可", true}, {"貫通の止め秒", 2.0f / 60.0f}}),
        0u);
    BreakableTackleRock* rock = scene.SpawnActor<BreakableTackleRock>("BreakableTackleRock");
    ASSERT_NE(rock, nullptr);
    ASSERT_EQ(NS::ApplyJsonFields(rock->Params(), nlohmann::json{{"耐久", 0.0f}}), 0u);
    rock->Root().SetPosition(NS::Vector3{0.0f, 0.5f, 0.6f});
    const std::vector<TackleStep> trace = RunTackle(scene, *player, 1.0f, 5);
    ASSERT_EQ(trace.size(), 5u);
    const GL::Level::ImpactRecord& impact = player->Resolver().Answer().lastImpact;
    ASSERT_TRUE(impact.broke);
    EXPECT_EQ(impact.hitStopSteps, 2);
    EXPECT_EQ(rock->stopOverride, 2);
    EXPECT_FALSE(trace[1].canMoveBody);
    EXPECT_FALSE(trace[2].canMoveBody);
    EXPECT_TRUE(trace[3].canMoveBody);
    EXPECT_FALSE(trace[3].inTackleHit);
    EXPECT_FALSE(trace[3].rebounding);
}

// 新しい当たりは、前の当たりの束で走っている演出を検知の歩までに打ち切り、次の歩に自分の束で始め直す
TEST(PlayerTackleHit, ANewHitStopsTheRunningSet)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTackleScene(scene, 0.0f);
    ASSERT_NE(player, nullptr);
    GL::Level::PropActor* rock = TackleRockOf(scene);
    ASSERT_NE(rock, nullptr);
    const std::vector<TackleStep> first = RunTackle(scene, *player, 1.0f, 16);
    ASSERT_EQ(first.size(), 16u);
    ASSERT_TRUE(first[15].sinkShown);
    ASSERT_TRUE(player->IsRebounding());

    // 飛んだ相手を自機の前へ戻し、反動の途中から当て直す
    rock->Root().SetPosition(player->Root().Position() + NS::Vector3{0.0f, -0.5f, 0.6f});
    const std::vector<TackleStep> second = RunTackle(scene, *player, 1.0f, 2);
    ASSERT_EQ(second.size(), 2u);
    ASSERT_EQ(player->Resolver().Answer().lastImpact.sequence, 2u);
    EXPECT_FALSE(second[0].sinkShown) << "前の当たりの沈みは検知の歩に止まる";
    EXPECT_TRUE(second[1].sinkShown) << "次の歩に新しい当たりの束で沈む";
}

namespace
{
    // 自機が出したエフェクトのうち、絵の名前が name の物の数
    int CountPlayerEffects(PlayerActor& player, std::string_view name)
    {
        int count = 0;
        for (const NS::EffectRecord& record : player.GetEffectKeeper()->Records())
        {
            if (record.name == name)
            {
                ++count;
            }
        }
        return count;
    }

    // .actor の束 setName から、持ち主へ渡す項目 ownerName を外す
    void RemoveOwnerItem(nlohmann::json& actorFile, std::string_view setName, std::string_view ownerName)
    {
        std::vector<NS::HitReactionSet> sets = NS::ReadHitReactionSets("PlayerActor", actorFile);
        bool removed = false;
        for (NS::HitReactionSet& set : sets)
        {
            if (set.name != setName)
            {
                continue;
            }
            std::erase_if(set.entries, [&](const NS::HitReactionEntry& entry) {
                const NS::HitOwnerItem* owner = std::get_if<NS::HitOwnerItem>(&entry.item);
                return owner != nullptr && owner->name == ownerName;
            });
            removed = true;
        }
        EXPECT_TRUE(removed) << setName;
        NS::WriteHitReactionSets(actorFile, sets);
    }
} // namespace

// 当たりの絵は段の束の持ち主へ渡す項目 HitEffect を始めた歩 (1 歩目) に、飛びの絵は明けの束の FlightEffect を
// 始めた明けの歩 (13 歩目) に出る。項目を外した束では出ない
TEST(PlayerTackleHit, TheHitAndFlightEffectsStartFromTheirOwnerItems)
{
    for (const bool removed : {false, true})
    {
        SCOPED_TRACE(removed);
        std::optional<ScopedActorFileEdit> edit;
        if (removed)
        {
            edit.emplace("PlayerActor", [](nlohmann::json& actorFile) {
                RemoveOwnerItem(actorFile, "体当たり[真ん中]", "HitEffect");
                RemoveOwnerItem(actorFile, "明け[真ん中]", "FlightEffect");
            });
        }
        NS::Scene scene;
        PlayerActor* player = PlaceTackleScene(scene, 0.0f);
        ASSERT_NE(player, nullptr);
        std::vector<int> cores;
        std::vector<int> trails;
        const std::vector<TackleStep> trace = RunTackle(scene, *player, 1.0f, 16, 0.0f, [&](int) {
            cores.push_back(CountPlayerEffects(*player, "impact.core"));
            trails.push_back(CountPlayerEffects(*player, "rebound.trail"));
        });
        ASSERT_EQ(trace.size(), 16u);
        ASSERT_TRUE(trace[13].rebounding);
        for (std::size_t k = 0; k < trace.size(); ++k)
        {
            SCOPED_TRACE(k);
            int expectedCores = 0;
            int expectedTrails = 0;
            if (!removed && k >= 1)
            {
                expectedCores = 1;
            }
            if (!removed && k >= 13)
            {
                expectedTrails = 1;
            }
            EXPECT_EQ(cores[k], expectedCores);
            EXPECT_EQ(trails[k], expectedTrails);
        }
    }
}

// 反動の着地は 1 つの問いが答え、答えが真のフレームに潰れが始まり、粉が出て、尾は消えている。
// 状態は次のフレームの頭で同じ問いを読み、立ちか滑りへ移る
TEST(PlayerTackleHit, TheReboundLandingFrameSquashesDustsAndEndsTheTrail)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTackleScene(scene, 0.0f);
    ASSERT_NE(player, nullptr);
    const std::vector<TackleStep> trace = RunTackle(scene, *player, 1.0f, 14);
    ASSERT_EQ(trace.size(), 14u);
    ASSERT_TRUE(trace.back().rebounding);
    ASSERT_EQ(CountPlayerEffects(*player, "rebound.trail"), 1);

    bool landed = false;
    for (int frame = 0; frame < 240 && !landed; ++frame)
    {
        const int dustBefore = CountPlayerEffects(*player, "land.dust");
        const float heightBefore = player->ModelClassObj()->GetDrawScale().y;
        scene.OnUpdate();
        if (!player->Motion().reboundLanded)
        {
            EXPECT_EQ(CountPlayerEffects(*player, "land.dust"), dustBefore);
            continue;
        }
        landed = true;
        EXPECT_EQ(CountPlayerEffects(*player, "land.dust"), dustBefore + 1);
        EXPECT_LT(player->ModelClassObj()->GetDrawScale().y, heightBefore);
        const NS::EffectRecord* trail = nullptr;
        int trails = 0;
        for (const NS::EffectRecord& record : player->GetEffectKeeper()->Records())
        {
            if (record.name == "rebound.trail")
            {
                trail = &record;
                ++trails;
            }
        }
        ASSERT_EQ(trails, 1) << "反動の尾を 1 本出した当たりで見る";
        EXPECT_TRUE(trail->rootStopStep.has_value() || trail->endStep.has_value());
        EXPECT_TRUE(player->IsRebounding());
        scene.OnUpdate();
        EXPECT_FALSE(player->IsRebounding());
        EXPECT_FALSE(player->Motion().reboundLanded);
    }
    EXPECT_TRUE(landed);
}

// 外れの核は当たりの絵の頭と次のフレームだけ写り、その次のフレームに薄れずに消える。火花は外した側へ流す
TEST(PlayerTackleHit, AMissCoreIsCutAfterTwoFramesAndSparksFollowTheFace)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTackleScene(scene, 0.75f);
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(RunTackle(scene, *player, 1.0f, 8).size(), 8u);
    const GL::Level::ImpactRecord& impact = player->Resolver().Answer().lastImpact;
    ASSERT_EQ(impact.tier, GL::Level::HitTier::Wide);

    const NS::EffectRecord* core = nullptr;
    const NS::EffectRecord* sparks = nullptr;
    for (const NS::EffectRecord& record : player->GetEffectKeeper()->Records())
    {
        if (record.name == "impact.core")
        {
            core = &record;
        }
        if (record.name == "impact.sparks")
        {
            sparks = &record;
        }
    }
    ASSERT_NE(core, nullptr);
    ASSERT_TRUE(core->endStep.has_value());
    EXPECT_EQ(core->endStep.value() - core->startStep, 2);
    EXPECT_FALSE(core->rootStopStep.has_value());

    ASSERT_NE(sparks, nullptr);
    ASSERT_TRUE(sparks->rotation.has_value());
    const NS::Vector3 heading = NS::Vector3::Transform(NS::Vector3{0.0f, 1.0f, 0.0f}, sparks->rotation.value());
    // 記録の面の向きは反動の配分が置物の球の面として読んだ向き
    const NS::Vector3 round =
        GL::Level::MissSurfaceNormal(impact.faceU, impact.faceV, 2.0f, player->BodySlamDirection());
    EXPECT_NEAR(impact.faceNormal.Dot(round), 1.0f, 1.0e-4f);
    const NS::Vector3 expected = player->ImpactVisuals().MissSparkHeading(
        impact.faceU, impact.faceV, impact.faceNormal, player->BodySlamDirection());
    ASSERT_GT(expected.Length(), 0.5f);
    EXPECT_NEAR(heading.Dot(expected), 1.0f, 1.0e-4f);
}

// 突進の線の先の相手に触れる前の、当たる直前の状態と動作を縛る。横軸は検知の歩を 0 にした歩

namespace
{
    // 自機以外の段で回った歩を数える
    class OthersTicker final : public NS::SceneSystem
    {
    public:
        void OnTick() override { ++ticks; }
        int ticks = 0;
    };

    // 検知までの 1 歩の自機と、自機以外の段が回ったか
    struct ContactStep
    {
        bool inBeforeContact = false;
        bool slamming = false;
        bool canMoveBody = true;
        int framesToContact = -1;
        int actionFrame = -1;
        NS::Vector3 drawScale{1.0f, 1.0f, 1.0f};
        bool othersRan = true;
        float advance = 0.0f;
        int flashRemaining = 0;
    };

    // +z へ突進を出し、検知の歩までの歩を並べる。最後の要素が検知の歩。当たらなければ 120 歩で止める
    // each は歩を読んだ後に、その歩の番号を渡して呼ぶ。真を返すとそこで止める
    std::vector<ContactStep> RunUntilContact(NS::Scene& scene,
                                             PlayerActor& player,
                                             const OthersTicker& others,
                                             float charge01 = 1.0f,
                                             const std::function<bool(std::size_t)>& each = {})
    {
        std::vector<ContactStep> steps;
        player.RequestBodySlam(charge01, NS::Vector3{0.0f, 0.0f, 1.0f});
        for (int frame = 0; frame < 120 && player.Resolver().Answer().lastImpact.sequence == 0; ++frame)
        {
            const int before = others.ticks;
            const float z = player.Root().Position().z;
            scene.OnUpdate();
            ContactStep step;
            step.inBeforeContact = player.IsInBeforeContact();
            step.slamming = player.IsBodySlamming();
            step.canMoveBody = player.CanMoveBody();
            step.framesToContact = player.Resolver().Answer().framesToPredictedContact;
            if (player.ActionTimelineClassObj()->IsRunning())
            {
                step.actionFrame = player.ActionTimelineClassObj()->Frame();
            }
            step.drawScale = player.ModelClassObj()->GetDrawScale();
            step.othersRan = others.ticks != before;
            step.advance = player.Root().Position().z - z;
            if (const NS::HitScreenDirector* screen = NS::FindSceneSystem<NS::HitScreenDirector>(&player))
            {
                step.flashRemaining = screen->FlashFramesRemaining();
            }
            steps.push_back(step);
            if (each && each(steps.size() - 1))
            {
                break;
            }
        }
        return steps;
    }

    // 出荷の自機の、真ん中の当たる直前の動作の形を +z へ向けた再生
    NS::DrawShapeDesc ShippedBeforeContactShape()
    {
        NS::DrawShapeDesc desc{};
        const nlohmann::json* actorFile = NS::ActorFileLibrary::Get().Find("PlayerActor");
        EXPECT_NE(actorFile, nullptr);
        if (actorFile == nullptr)
        {
            return desc;
        }
        for (const NS::ActorAction& action : NS::ReadActorActions("PlayerActor", *actorFile))
        {
            if (action.name != GL::Level::BeforeContactActionName(GL::Level::HitTier::Center))
            {
                continue;
            }
            for (const NS::ActorActionEvent& event : action.events)
            {
                if (const NS::HitShapeItem* shape = std::get_if<NS::HitShapeItem>(&event.item))
                {
                    desc = NS::DrawShapeDesc{.along = shape->along,
                                             .height = shape->height,
                                             .side = shape->side,
                                             .alongDirection = NS::Vector3{0.0f, 0.0f, 1.0f},
                                             .length = event.length};
                }
            }
        }
        EXPECT_EQ(desc.length, 7) << "形が接触の 6 歩前から検知の歩までの、出荷の動作で見る";
        return desc;
    }

    // 検知の歩を 0 にした歩 rel の、出荷の当たる直前の形。形の長さの外は元の形
    NS::Vector3 ExpectedBeforeContactScale(const NS::DrawShapeDesc& shape, int rel)
    {
        const int frame = rel + shape.length - 1;
        if (frame < 0 || frame >= shape.length)
        {
            return NS::Vector3{1.0f, 1.0f, 1.0f};
        }
        return shape.ScaleAt(frame);
    }

    void ExpectScaleNear(const NS::Vector3& actual, const NS::Vector3& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, 1.0e-5f);
        EXPECT_NEAR(actual.y, expected.y, 1.0e-5f);
        EXPECT_NEAR(actual.z, expected.z, 1.0e-5f);
    }
} // namespace

// 真ん中の予測では、接触の 6 歩前に当たる直前の状態へ入って縮み始め、2 歩前から自機以外が止まる。
// 自機は止まらずに突進の速さで進み、検知の歩で当たる直前を出る
TEST(PlayerBeforeContact, TheCenterPredictionShrinksFromSixStepsAndHoldsTheOthersFromTwo)
{
    OthersTicker others;
    NS::Scene scene;
    PlayerActor* player = PlaceTackleScene(scene, 0.0f);
    ASSERT_NE(player, nullptr);
    scene.AddSystemTick(others, NS::UpdatePhase::Triggers);
    const NS::DrawShapeDesc shape = ShippedBeforeContactShape();
    const std::vector<ContactStep> steps = RunUntilContact(scene, *player, others);
    scene.RemoveSystemTick(others);
    ASSERT_NE(player->Resolver().Answer().lastImpact.sequence, 0u);
    ASSERT_EQ(player->Resolver().Answer().lastImpact.tier, GL::Level::HitTier::Center);
    ASSERT_GE(steps.size(), 9u) << "当たる直前の 6 歩より前から突進している間合いで見る";
    const int detect = static_cast<int>(steps.size()) - 1;
    // 自機以外を止める前の歩の進みを、突進の速さの 1 歩とする
    const float slamStep = steps[static_cast<std::size_t>(detect - 3)].advance;
    ASSERT_GT(slamStep, 0.0f);
    for (int i = 1; i <= detect; ++i)
    {
        const int rel = i - detect;
        SCOPED_TRACE(rel);
        const ContactStep& step = steps[static_cast<std::size_t>(i)];
        EXPECT_EQ(step.inBeforeContact, rel >= -6 && rel <= -1);
        ExpectScaleNear(step.drawScale, ExpectedBeforeContactScale(shape, rel));
        const bool held = rel == -2 || rel == -1;
        EXPECT_EQ(step.othersRan, !held);
        if (rel < 0)
        {
            EXPECT_TRUE(step.slamming);
            EXPECT_TRUE(step.canMoveBody);
        }
        if (held)
        {
            EXPECT_NEAR(step.advance, slamStep, slamStep * 0.05f);
        }
        if (rel == -6)
        {
            EXPECT_EQ(step.framesToContact, 6);
            EXPECT_EQ(step.actionFrame, 0);
        }
    }
    EXPECT_FALSE(scene.IsHoldingOthers());
}

// 外れの段には当たる直前の動作が無いので、外れの予測では当たる直前に入らず、縮まず止めない
TEST(PlayerBeforeContact, AMissPredictionNeitherShrinksNorHoldsTheOthers)
{
    OthersTicker others;
    NS::Scene scene;
    PlayerActor* player = PlaceTackleScene(scene, 0.75f);
    ASSERT_NE(player, nullptr);
    scene.AddSystemTick(others, NS::UpdatePhase::Triggers);
    const std::vector<ContactStep> steps = RunUntilContact(scene, *player, others);
    scene.RemoveSystemTick(others);
    ASSERT_NE(player->Resolver().Answer().lastImpact.sequence, 0u);
    ASSERT_EQ(player->Resolver().Answer().lastImpact.tier, GL::Level::HitTier::Wide);
    bool predicted = false;
    for (std::size_t i = 0; i < steps.size(); ++i)
    {
        SCOPED_TRACE(i);
        const ContactStep& step = steps[i];
        predicted = predicted || step.framesToContact > 0;
        EXPECT_FALSE(step.inBeforeContact);
        EXPECT_TRUE(step.othersRan);
        ExpectScaleNear(step.drawScale, NS::Vector3{1.0f, 1.0f, 1.0f});
    }
    EXPECT_TRUE(predicted) << "線の先の外れの相手は予測している";
}

// 予測した相手が線から外れた時と、段が変わった時は、当たる直前を出て突進へ戻り、形・自機以外の止め・
// 当たる直前の動作で始めた白の光を戻す
TEST(PlayerBeforeContact, AMissedPredictionRestoresTheShapeTheOthersAndTheReturns)
{
    const ScopedActorFileEdit flash("PlayerActor", [](nlohmann::json& actorFile) {
        std::vector<NS::ActorAction> actions = NS::ReadActorActions("PlayerActor", actorFile);
        for (NS::ActorAction& action : actions)
        {
            if (action.name == GL::Level::BeforeContactActionName(GL::Level::HitTier::Center))
            {
                // 動作は検知のフレームで終わるので、白の光も動作の長さの内に置く
                action.events.push_back(NS::ActorActionEvent{.item = NS::HitFlashItem{}, .start = 0, .length = 7});
            }
        }
        NS::WriteActorActions(actorFile, actions);
    });
    for (const float shift : {10.0f, 0.75f})
    {
        SCOPED_TRACE(shift);
        OthersTicker others;
        NS::Scene scene;
        PlayerActor* player = PlaceTackleScene(scene, 0.0f);
        ASSERT_NE(player, nullptr);
        GL::Level::PropActor* rock = TackleRockOf(scene);
        ASSERT_NE(rock, nullptr);
        const std::vector<ContactStep> steps =
            RunUntilContact(scene, *player, others, 1.0f, [&](std::size_t) { return scene.IsHoldingOthers(); });
        ASSERT_FALSE(steps.empty());
        ASSERT_TRUE(scene.IsHoldingOthers());
        ASSERT_TRUE(steps.back().inBeforeContact);
        ASSERT_GT(steps.back().flashRemaining, 0);
        ASSERT_EQ(player->Resolver().Answer().lastImpact.sequence, 0u);
        // 10 は線の外へ、0.75 は線の上のまま外れの段へ退ける
        rock->Root().SetPosition(rock->Root().Position() + NS::Vector3{shift, 0.0f, 0.0f});
        scene.OnUpdate();
        EXPECT_FALSE(player->IsInBeforeContact());
        EXPECT_TRUE(player->IsBodySlamming());
        EXPECT_FALSE(scene.IsHoldingOthers());
        ExpectScaleNear(player->ModelClassObj()->GetDrawScale(), NS::Vector3{1.0f, 1.0f, 1.0f});
        const NS::HitScreenDirector* screen = NS::FindSceneSystem<NS::HitScreenDirector>(player);
        ASSERT_NE(screen, nullptr);
        EXPECT_EQ(screen->FlashFramesRemaining(), 0);
        EXPECT_EQ(player->Resolver().Answer().lastImpact.sequence, 0u);
    }
}

// 予測が遅れて接触の N 歩前 (N < 6) に入った時は、当たる直前の状態と動作を 6 − N フレーム目から始める。
// 形は遅れずに入った時と同じ歩に同じ倍率を描き、入ったフレームより前に置いた自機以外の止めは起きない
TEST(PlayerBeforeContact, ALatePredictionEntersAtTheFrameThatMatchesTheRest)
{
    struct LateCase
    {
        float rockZ;
        int entered; // 入った歩の、接触までの見込みのフレーム数
        bool holdsOthers;
    };
    const NS::DrawShapeDesc shape = ShippedBeforeContactShape();
    for (const LateCase& late : {LateCase{2.6f, 3, true}, LateCase{2.0f, 1, false}})
    {
        SCOPED_TRACE(late.entered);
        OthersTicker others;
        NS::Scene scene;
        PlayerActor* player = PlaceTackleScene(scene, 0.0f, late.rockZ);
        ASSERT_NE(player, nullptr);
        scene.AddSystemTick(others, NS::UpdatePhase::Triggers);
        const std::vector<ContactStep> steps = RunUntilContact(scene, *player, others);
        scene.RemoveSystemTick(others);
        ASSERT_NE(player->Resolver().Answer().lastImpact.sequence, 0u);
        ASSERT_EQ(player->Resolver().Answer().lastImpact.tier, GL::Level::HitTier::Center);
        const int detect = static_cast<int>(steps.size()) - 1;
        std::optional<int> enteredAt;
        for (int i = 0; i < detect; ++i)
        {
            if (steps[static_cast<std::size_t>(i)].inBeforeContact)
            {
                enteredAt = i;
                break;
            }
        }
        ASSERT_TRUE(enteredAt.has_value());
        const ContactStep& entry = steps[static_cast<std::size_t>(*enteredAt)];
        ASSERT_EQ(entry.framesToContact, late.entered) << "予測が遅れて入る間合いで見る";
        EXPECT_EQ(*enteredAt - detect, -late.entered);
        EXPECT_EQ(entry.actionFrame, 6 - late.entered);
        bool othersHeld = false;
        for (int i = *enteredAt; i <= detect; ++i)
        {
            const int rel = i - detect;
            SCOPED_TRACE(rel);
            const ContactStep& step = steps[static_cast<std::size_t>(i)];
            ExpectScaleNear(step.drawScale, ExpectedBeforeContactScale(shape, rel));
            othersHeld = othersHeld || !step.othersRan;
        }
        EXPECT_EQ(othersHeld, late.holdsOthers);
    }
}

// 線の先の相手に触れる見込みのフレーム数は、当たる直前の動作の長さに依らず数える。検知の 1 フレーム前で 1
TEST(PlayerBeforeContact, FramesToPredictedContactCountDownToTheDetection)
{
    OthersTicker others;
    NS::Scene scene;
    PlayerActor* player = PlaceTackleScene(scene, 0.0f, 6.0f);
    ASSERT_NE(player, nullptr);
    const std::vector<ContactStep> steps = RunUntilContact(scene, *player, others);
    ASSERT_NE(player->Resolver().Answer().lastImpact.sequence, 0u);
    ASSERT_GE(steps.size(), 9u);
    const std::size_t last = steps.size() - 1;
    EXPECT_EQ(steps[last].framesToContact, -1);
    for (int k = 1; k <= 7; ++k)
    {
        SCOPED_TRACE(k);
        EXPECT_EQ(steps[last - static_cast<std::size_t>(k)].framesToContact, k);
    }
}

// 突進の残りの距離で届かない相手は、線の先にいても触れる見込みに入れず、当たる直前に入らない
TEST(PlayerBeforeContact, ATargetBeyondTheRemainingSlamIsNotPredicted)
{
    OthersTicker others;
    NS::Scene scene;
    PlayerActor* player = PlaceTackleScene(scene, 0.0f, 9.0f);
    ASSERT_NE(player, nullptr);
    const NS::Vector3 forward{0.0f, 0.0f, 1.0f};
    GL::Level::SlamLineTarget reachable{};
    ASSERT_TRUE(player->Resolver().FindSlamLineTarget(
        forward, player->BodySlamDistance(), &player->Params(), &player->Body(), &player->Collider(), reachable))
        << "溜めた突進の距離なら線の先に入る相手で見る";
    // 溜め量 0 の通常突進は、溜めた突進より短い距離で終わる
    int slammingSteps = 0;
    const std::vector<ContactStep> steps = RunUntilContact(scene, *player, others, 0.0f, [&](std::size_t) {
        if (player->IsBodySlamming())
        {
            ++slammingSteps;
        }
        return slammingSteps > 0 && !player->IsBodySlamming();
    });
    ASSERT_GT(slammingSteps, 0);
    ASSERT_FALSE(player->IsBodySlamming());
    ASSERT_EQ(player->Resolver().Answer().lastImpact.sequence, 0u) << "通常突進が届かない所に置いた相手で見る";
    for (std::size_t i = 0; i < steps.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(steps[i].framesToContact, -1);
        EXPECT_FALSE(steps[i].inBeforeContact);
    }
}
