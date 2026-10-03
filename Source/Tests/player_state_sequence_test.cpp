#include "Game/Player.h"
#include "Game/Player/PlayerGravity.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/LedgeClimbingPlayerState.h"
#include "Game/Player/States/ReboundPlayerState.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/HitReaction.h"
#include "Runtime/Object/Reflection/ReflectionJson.h"
#include "Runtime/Object/Scene/Scene.h"

#include <gtest/gtest.h>

#include <type_traits>
#include <utility>

static_assert(std::is_base_of_v<NS::Obj::StateOf<NS::Game::Player::IdlePlayerState, ::Player>,
                                NS::Game::Player::IdlePlayerState>);

namespace
{
    class ObservePlayerEffectsState final : public NS::Obj::StateOf<ObservePlayerEffectsState, ::Player>
    {
    public:
        void OnStep(::Player& player, float) override
        {
            EXPECT_EQ(player.HitReactionPart()->FlashFramesRemaining(), 3);
        }
    };

    Player* PlaceSequencePlayer(NS::Obj::Scene& scene)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json entry = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(entry, "Player");
        NS::Obj::SetObjectJsonId(entry, 1);
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(entry));
        scene.LoadJson(doc);
        return static_cast<Player*>(scene.Objects().FindByObjectId(1));
    }
} // namespace

TEST(PlayerStateSequence, LedgeClimbKeepsItsTwoStageTiming)
{
    NS::Obj::Scene scene;
    Player* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::Obj::Body* movement = NS::Obj::ComponentCast<NS::Obj::Body>(player->Part("Movement"));
    ASSERT_NE(movement, nullptr);
    player->Root().SetPosition(NS::Core::Vector3{});
    player->ClimbLedge();
    ASSERT_TRUE(player->States().IsCurrent<NS::Game::Player::LedgeClimbingPlayerState>());
    EXPECT_FLOAT_EQ(player->Root().Position().y, 0.0f);
    const float top = movement->CapsuleHalfHeight() + movement->CapsuleRadius();
    player->States().Step(0.0625f);
    EXPECT_NEAR(player->Root().Position().y, top * 0.5f, 0.00001f);
    player->States().Step(0.0625f);
    EXPECT_NEAR(player->Root().Position().y, top, 0.00001f);
    player->States().Step(0.125f);
    EXPECT_TRUE(player->States().IsCurrent<NS::Game::Player::IdlePlayerState>());
    EXPECT_TRUE(movement->IsGrounded());
}

TEST(PlayerStateSequence, LeavingClimbCannotResumeAnOldPositionWrite)
{
    NS::Obj::Scene scene;
    Player* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::Obj::Body* movement = NS::Obj::ComponentCast<NS::Obj::Body>(player->Part("Movement"));
    ASSERT_NE(movement, nullptr);
    player->ClimbLedge();
    player->States().Step(0.0625f);
    ASSERT_TRUE(player->States().Change<NS::Game::Player::IdlePlayerState>());
    player->Root().SetPosition(NS::Core::Vector3{0.0f, 3.0f, 0.0f});
    player->States().Step(0.0625f);
    EXPECT_FLOAT_EQ(player->Root().Position().y, 3.0f);
}

TEST(PlayerStateSequence, OnlyActorUpdateAdvancesTheOwnedStateMachine)
{
    NS::Obj::Scene scene;
    Player* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    player->Body().SetGrounded(true);
    ASSERT_TRUE(player->States().IsCurrent<NS::Game::Player::IdlePlayerState>());
    EXPECT_EQ(player->States().StepsInState(), 0u);
    player->Body().SetGrounded(true);
    player->Update();
    EXPECT_EQ(player->States().StepsInState(), 1u);
}

TEST(PlayerStateSequence, InactiveMovementStopsTheActorStateStep)
{
    NS::Obj::Scene scene;
    Player* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    player->Body().SetGrounded(true);
    player->Body().SetActive(false);
    player->Update();
    EXPECT_EQ(player->States().StepsInState(), 0u);
}

TEST(PlayerStateSequence, EffectsAdvanceAfterTheActorStateStep)
{
    NS::Obj::Scene scene;
    Player* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::Obj::HitReaction* reaction = player->HitReactionPart();
    ASSERT_NE(reaction, nullptr);
    reaction->Play(NS::Obj::HitReactionDesc{.flashFrames = 3, .flashAlpha = 1.0f});
    reaction->OnUpdate();
    ASSERT_EQ(reaction->FlashFramesRemaining(), 3);
    player->States().Build<ObservePlayerEffectsState>(*player);
    player->Update();
    EXPECT_EQ(player->States().StepsInState(), 1u);
    EXPECT_EQ(reaction->FlashFramesRemaining(), 2);
}

TEST(PlayerStateSequence, FrozenMovementDoesNotFreezeEffects)
{
    NS::Obj::Scene scene;
    Player* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::Obj::HitReaction* reaction = player->HitReactionPart();
    ASSERT_NE(reaction, nullptr);
    reaction->Play(NS::Obj::HitReactionDesc{.flashFrames = 3, .flashAlpha = 1.0f});
    reaction->OnUpdate();
    player->Body().SetActive(false);
    player->Update();
    EXPECT_EQ(player->States().StepsInState(), 0u);
    EXPECT_EQ(reaction->FlashFramesRemaining(), 2);
}

TEST(PlayerStateSequence, ReboundKeepsGravityOrderAndIgnoresJump)
{
    NS::Obj::Scene scene;
    Player* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::Obj::Body* movement = NS::Obj::ComponentCast<NS::Obj::Body>(player->Part("Movement"));
    ASSERT_NE(movement, nullptr);
    movement->SetGrounded(true);
    const NS::Game::Player::ReboundArc arc{.apexHeight = 1.0f, .distance = 5.0f};
    const NS::Core::Vector3 initial = player->ReboundVelocityFor(arc);
    ASSERT_TRUE(player->BeginRebound(arc));
    EXPECT_FLOAT_EQ(movement->VerticalVelocity(), initial.y);
    player->SetJumpPressed();
    player->States().Step(0.01f);
    EXPECT_NEAR(movement->VerticalVelocity(), initial.y - 0.125f, 0.00001f);
    EXPECT_TRUE(player->States().IsCurrent<NS::Game::Player::ReboundPlayerState>());
    player->States().Step(0.02f);
    EXPECT_NEAR(movement->VerticalVelocity(), initial.y - 0.375f, 0.00001f);
    movement->SetVerticalVelocity(0.0f);
    player->States().Step(0.01f);
    EXPECT_TRUE(player->States().IsCurrent<NS::Game::Player::IdlePlayerState>());
}

// 反動の 1 フレームに当てる重力は、普段と同じ選び方 ChooseGravity へ反動の組 (上りだけ倍率付き) を渡した値
// 頂点の帯 (縦の速さの大きさが頂点滞空 Vy 未満) を反動でも同じ式で見るので、1 と -1 の場合が帯を外すと割れる
TEST(PlayerStateSequence, ReboundGravityIsTheChoiceWithTheReboundRise)
{
    NS::Obj::Scene scene;
    Player* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(NS::Obj::ApplyJsonFields(player->Params(), {{"頂点滞空 Vy", 3.0f}, {"頂点滞空倍率", 0.3f}}), 0u);
    constexpr float k_Dt = 1.0f / 60.0f;
    const NS::Game::Player::PlayerGravity gravity = player->Params().ReboundGravity();
    for (const float vertical : {5.0f, 1.0f, -1.0f, -5.0f})
    {
        SCOPED_TRACE(vertical);
        player->Body().SetVerticalVelocity(vertical);
        player->ReboundGravity(k_Dt);
        EXPECT_FLOAT_EQ(player->Body().VerticalVelocity(),
                        vertical + NS::Game::Player::ChooseGravity(gravity, vertical) * k_Dt);
    }
}

// 突進の玉は、丸まる前の立ち姿の下の球がそのまま玉になる。丸まる時の根の下げ幅 (ChangeCurled) と玉の中心の決まりが結ばれている
// 下げ幅を変えると、判定・矢印・エディタの面が読む玉だけが黙ってずれる
TEST(PlayerStateSequence, SlamBallStaysPutWhenCurling)
{
    NS::Obj::Scene scene;
    Player* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    player->Root().SetPosition(NS::Core::Vector3{1.0f, 2.0f, 3.0f});
    ASSERT_FALSE(player->IsCurled());
    const NS::Core::Vector3 standingRoot = player->Root().Position();
    const NS::Core::Sphere standing = player->SlamBallAt(standingRoot);
    EXPECT_FLOAT_EQ(standing.center.y, standingRoot.y - player->Body().StandingHalfHeight());
    EXPECT_FLOAT_EQ(standing.radius, player->Body().CapsuleRadius());

    player->SetCurled(true);
    ASSERT_TRUE(player->IsCurled());
    const NS::Core::Sphere curled = player->SlamBallAt(player->Root().Position());
    EXPECT_FLOAT_EQ(curled.center.x, standing.center.x);
    EXPECT_FLOAT_EQ(curled.center.y, standing.center.y);
    EXPECT_FLOAT_EQ(curled.center.z, standing.center.z);
    EXPECT_FLOAT_EQ(curled.radius, standing.radius);
}

TEST(PlayerStateSequence, EndPlayCancelsTheClimbBeforePartsLeave)
{
    NS::Obj::Scene scene;
    Player* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::Obj::Body* movement = NS::Obj::ComponentCast<NS::Obj::Body>(player->Part("Movement"));
    ASSERT_NE(movement, nullptr);
    player->ClimbLedge();
    player->States().Step(0.0625f);
    player->OnEndPlay();
    player->Root().SetPosition(NS::Core::Vector3{0.0f, 3.0f, 0.0f});
    player->States().Step(0.0625f);
    EXPECT_FLOAT_EQ(player->Root().Position().y, 3.0f);
}

TEST(PlayerStateSequence, TheBaseOwnsTheMachineFromConstruction)
{
    Player player;
    ASSERT_NE(player.GetStateMachine(), nullptr);
    EXPECT_EQ(player.GetStateMachine()->CurrentId(), NS::Obj::StateIdOf<NS::Game::Player::IdlePlayerState>());
    EXPECT_TRUE(NS::Obj::IsState<NS::Game::Player::IdlePlayerState>(player));
}

TEST(PlayerStateSequence, ReboundAndBodySlamAreNeverBothTrue)
{
    NS::Obj::Scene scene;
    Player* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::Obj::Body& movement = player->Body();
    movement.SetGrounded(true);
    ASSERT_TRUE(player->BeginRebound(NS::Game::Player::ReboundArc{.apexHeight = 1.0f, .distance = 5.0f}));
    EXPECT_TRUE(player->IsRebounding());
    EXPECT_FALSE(player->IsBodySlamming());

    player->RequestBodySlam(0.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    EXPECT_TRUE(player->IsBodySlamming());
    EXPECT_FALSE(player->IsRebounding());

    ASSERT_TRUE(player->BeginRebound(NS::Game::Player::ReboundArc{.apexHeight = 1.0f, .distance = 5.0f}));
    EXPECT_TRUE(player->IsRebounding());
    EXPECT_FALSE(player->IsBodySlamming());
}
