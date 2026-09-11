#include "Game/Player/PlayerComponent.h"
#include "Game/Player/PlayerState.h"

#include "Game/Entity/EntityStateManagerComponent.h"

namespace NS::Game::Player
{
    //! 走り。移る先は落下と立ち
    class WalkPlayerState final : public PlayerState
    {
    public:
        static constexpr const char* k_Name = "Walk";

        [[nodiscard]] const char* Name() const noexcept override { return k_Name; }

        void OnStep(PlayerComponent& player, float dt) override
        {
            player.TickTimers(dt);
            player.AccelerateToInputDirection(dt);
            player.Jump(dt);
            player.CutJumpRelease();
            player.Gravity(dt);
            player.Move(dt);
            player.SyncGroundState();
            if (player.LedgeGrab())
                return;

            if (player.ShouldFall())
                player.States()->ChangeByName("Fall");
            else if (player.ShouldIdle())
                player.States()->ChangeByName(PlayerComponent::k_IdleStateName);
        }
    };

    NS_STATE(WalkPlayerState, NS::Game::Player::PlayerComponent)
} // namespace NS::Game::Player
