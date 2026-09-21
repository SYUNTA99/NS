#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerStateManagerComponent.h>
#include <Game/Player/States/FallPlayerState.h>
#include <Game/Player/States/IdlePlayerState.h>
#include <Runtime/Object/GameObject.h>
#include <gtest/gtest.h>

namespace
{
    using NS::Game::Player::FallPlayerState;
    using NS::Game::Player::IdlePlayerState;
    using NS::Game::Player::PlayerComponent;
    using NS::Game::Player::PlayerStateManagerComponent;
    using NS::Object::GameObject;

    constexpr float k_FixedDt = 1.0f / 60.0f;

    // 組んだ直後を見たい所は Step でなく EnsureBuilt を呼ぶ。Step は状態を 1 回走らせるので、
    // 床の無いこの検証台では立ちから落下へ移ってしまう
    struct Rig
    {
        GameObject owner;
        PlayerStateManagerComponent* manager = nullptr;
        PlayerComponent* player = nullptr;

        Rig()
        {
            manager = owner.AddComponent<PlayerStateManagerComponent>();
            player = owner.AddComponent<PlayerComponent>();
            player->OnStart();
            manager->OnStart();
        }
    };
} // namespace

TEST(PlayerStateManagerTest, NothingIsBuiltBeforeTheFirstStep)
{
    Rig rig;

    EXPECT_FALSE(rig.manager->IsBuilt());
    EXPECT_STREQ(rig.manager->CurrentName(), "");
}

TEST(PlayerStateManagerTest, BuildingEntersIdle)
{
    Rig rig;

    rig.manager->EnsureBuilt(*rig.player);

    EXPECT_TRUE(rig.manager->IsBuilt());
    EXPECT_TRUE(rig.manager->IsCurrent<IdlePlayerState>());
}

TEST(PlayerStateManagerTest, FirstStepBuildsTheMachine)
{
    Rig rig;

    rig.manager->Step(*rig.player, k_FixedDt);

    EXPECT_TRUE(rig.manager->IsBuilt());
}

TEST(PlayerStateManagerTest, ChangeMovesToTheState)
{
    Rig rig;
    rig.manager->EnsureBuilt(*rig.player);

    EXPECT_TRUE(rig.manager->Change<FallPlayerState>());
    EXPECT_TRUE(rig.manager->IsCurrent<FallPlayerState>());
    EXPECT_STREQ(rig.manager->CurrentName(), FallPlayerState::k_Name);
}

TEST(PlayerStateManagerTest, ResetToFirstReturnsToTheFirstState)
{
    Rig rig;
    rig.manager->EnsureBuilt(*rig.player);
    ASSERT_TRUE(rig.manager->Change<FallPlayerState>());

    rig.manager->ResetToFirst();

    EXPECT_TRUE(rig.manager->IsCurrent<IdlePlayerState>());
}

TEST(PlayerStateManagerTest, ChangeFailsBeforeTheMachineIsBuilt)
{
    GameObject owner;
    PlayerStateManagerComponent& manager = *owner.AddComponent<PlayerStateManagerComponent>();

    EXPECT_FALSE(manager.Change<FallPlayerState>());
    EXPECT_FALSE(manager.IsCurrent<IdlePlayerState>());
}
