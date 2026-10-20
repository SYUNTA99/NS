#include "Game/Level/ImpactResolver.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerGravity.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/LedgeClimbingPlayerState.h"
#include "Game/Player/States/ReboundPlayerState.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IUse/IUseSceneSystem.h"
#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/CoreObject/Reflection/AssetField.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/System/HitScreenDirector.h"

#include <gtest/gtest.h>

#include <type_traits>
#include <utility>

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
        nlohmann::json entry = NS::MakeObjectJson();
        NS::SetObjectJsonActorFile(entry, "PlayerActor");
        NS::SetObjectJsonId(entry, 1);
        NS::SceneJsonObjects(doc).push_back(std::move(entry));
        scene.ResetFromJson(doc);
        return static_cast<PlayerActor*>(scene.Objects().FindByObjectId(1));
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
    nlohmann::json entry = NS::MakeObjectJson();
    NS::SetObjectJsonActorFile(entry, "PlayerActor");
    NS::SetObjectJsonId(entry, 1);
    NS::SetObjectPosition(entry, NS::Vector3{0.0f, 1.0f, 0.0f});
    NS::SceneJsonObjects(doc).push_back(std::move(entry));
    nlohmann::json rock = NS::MakeObjectJson();
    NS::SetObjectJsonActorFile(rock, "PropActor");
    NS::SetObjectJsonId(rock, 2);
    NS::SetObjectPosition(rock, NS::Vector3{0.0f, 0.5f, 0.6f});
    NS::SceneJsonObjects(doc).push_back(std::move(rock));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    NS::OBB floor{};
    floor.center = NS::Vector3{0.0f, -0.5f, 0.0f};
    floor.halfExtentX = 100.0f;
    floor.halfExtentY = 0.5f;
    floor.halfExtentZ = 100.0f;
    scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
    PlayerActor* player = static_cast<PlayerActor*>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(player, nullptr);
    player->RequestBodySlam(1.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    for (int frame = 0; frame < 10 && !player->Resolver().Answer().freezeBegan; ++frame)
    {
        player->Update();
    }
    ASSERT_TRUE(player->Resolver().Answer().freezeBegan);
    NS::HitScreenDirector* screen = NS::FindSceneSystem<NS::HitScreenDirector>(player);
    ASSERT_NE(screen, nullptr);
    screen->StartFlash(*player, 3, 1.0f);
    screen->OnTick();
    const NS::uint32 stateStep = player->States().StepsInState();
    scene.OnUpdate();
    ASSERT_TRUE(player->Resolver().Answer().hitStopping);
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
    const GL::Player::PlayerGravity gravity = player->Params().ReboundGravity();
    for (const float vertical : {5.0f, 1.0f, -1.0f, -5.0f})
    {
        SCOPED_TRACE(vertical);
        player->Body().SetVerticalVelocity(vertical);
        player->ReboundGravity(k_Dt);
        EXPECT_FLOAT_EQ(player->Body().VerticalVelocity(),
                        vertical + GL::Player::ChooseGravity(gravity, vertical) * k_Dt);
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
