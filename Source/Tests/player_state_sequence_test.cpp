#include "Game/Entity/EntityComponent.h"
#include "Game/Player.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/LedgeClimbingPlayerState.h"
#include "Game/Player/States/ReboundPlayerState.h"
#include "Runtime/Object/Components/HitReaction.h"
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
    NS::Game::Entity::EntityComponent* movement =
        NS::Obj::ComponentCast<NS::Game::Entity::EntityComponent>(player->Part("Movement"));
    ASSERT_NE(movement, nullptr);
    player->Root().SetPosition(NS::Core::Vector3{});
    player->ClimbLedge();
    ASSERT_TRUE(player->States().IsCurrent<NS::Game::Player::LedgeClimbingPlayerState>());
    EXPECT_FLOAT_EQ(player->Root().Position().y, 0.0f);
    const float top = movement->CapsuleHalfHeight() + movement->CapsuleRadius();
    player->States().Step(*player, 0.0625f);
    EXPECT_NEAR(player->Root().Position().y, top * 0.5f, 0.00001f);
    player->States().Step(*player, 0.0625f);
    EXPECT_NEAR(player->Root().Position().y, top, 0.00001f);
    player->States().Step(*player, 0.125f);
    EXPECT_TRUE(player->States().IsCurrent<NS::Game::Player::IdlePlayerState>());
    EXPECT_TRUE(movement->IsGrounded());
}

TEST(PlayerStateSequence, LeavingClimbCannotResumeAnOldPositionWrite)
{
    NS::Obj::Scene scene;
    Player* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::Game::Entity::EntityComponent* movement =
        NS::Obj::ComponentCast<NS::Game::Entity::EntityComponent>(player->Part("Movement"));
    ASSERT_NE(movement, nullptr);
    player->ClimbLedge();
    player->States().Step(*player, 0.0625f);
    ASSERT_TRUE(player->States().Change<NS::Game::Player::IdlePlayerState>(*player));
    player->Root().SetPosition(NS::Core::Vector3{0.0f, 3.0f, 0.0f});
    player->States().Step(*player, 0.0625f);
    EXPECT_FLOAT_EQ(player->Root().Position().y, 3.0f);
}

TEST(PlayerStateSequence, OnlyActorUpdateAdvancesTheOwnedStateMachine)
{
    NS::Obj::Scene scene;
    Player* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    player->Body().SetGrounded(true);
    ASSERT_TRUE(player->States().IsCurrent<NS::Game::Player::IdlePlayerState>());
    EXPECT_EQ(player->States().StateStep(), 0u);
    player->Body().SetGrounded(true);
    player->Update();
    EXPECT_EQ(player->States().StateStep(), 1u);
}

TEST(PlayerStateSequence, InactiveMovementStopsTheActorStateStep)
{
    NS::Obj::Scene scene;
    Player* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    player->Body().SetGrounded(true);
    player->Body().SetActive(false);
    player->Update();
    EXPECT_EQ(player->States().StateStep(), 0u);
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
    EXPECT_EQ(player->States().StateStep(), 1u);
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
    EXPECT_EQ(player->States().StateStep(), 0u);
    EXPECT_EQ(reaction->FlashFramesRemaining(), 2);
}

TEST(PlayerStateSequence, ReboundKeepsGravityOrderAndIgnoresJump)
{
    NS::Obj::Scene scene;
    Player* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::Game::Entity::EntityComponent* movement =
        NS::Obj::ComponentCast<NS::Game::Entity::EntityComponent>(player->Part("Movement"));
    ASSERT_NE(movement, nullptr);
    movement->SetGrounded(true);
    const NS::Game::Player::ReboundArc arc{.apexHeight = 1.0f, .distance = 5.0f};
    const NS::Core::Vector3 initial = player->ReboundVelocityFor(arc);
    ASSERT_TRUE(player->BeginRebound(arc));
    EXPECT_FLOAT_EQ(movement->VerticalVelocity(), initial.y);
    player->SetJumpPressed();
    player->States().Step(*player, 0.01f);
    EXPECT_NEAR(movement->VerticalVelocity(), initial.y - 0.125f, 0.00001f);
    EXPECT_TRUE(player->States().IsCurrent<NS::Game::Player::ReboundPlayerState>());
    player->States().Step(*player, 0.02f);
    EXPECT_NEAR(movement->VerticalVelocity(), initial.y - 0.375f, 0.00001f);
    movement->SetVerticalVelocity(0.0f);
    player->States().Step(*player, 0.01f);
    EXPECT_TRUE(player->States().IsCurrent<NS::Game::Player::IdlePlayerState>());
}

TEST(PlayerStateSequence, EndPlayCancelsTheClimbBeforePartsLeave)
{
    NS::Obj::Scene scene;
    Player* player = PlaceSequencePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::Game::Entity::EntityComponent* movement =
        NS::Obj::ComponentCast<NS::Game::Entity::EntityComponent>(player->Part("Movement"));
    ASSERT_NE(movement, nullptr);
    player->ClimbLedge();
    player->States().Step(*player, 0.0625f);
    player->OnEndPlay();
    player->Root().SetPosition(NS::Core::Vector3{0.0f, 3.0f, 0.0f});
    player->States().Step(*player, 0.0625f);
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
    NS::Game::Entity::EntityComponent& movement = player->Body();
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
