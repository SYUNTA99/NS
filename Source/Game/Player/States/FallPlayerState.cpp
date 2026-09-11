#include "Game/Player/PlayerComponent.h"
#include "Game/Player/PlayerState.h"

#include "Game/Entity/EntityStateManagerComponent.h"

namespace NS::Game::Player
{
    //! 落下。着地した歩に走りか立ちへ移る
    class FallPlayerState final : public PlayerState
    {
    public:
        static constexpr const char* k_Name = "Fall";

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

            if (player.IsGrounded())
            {
                if (player.ShouldWalk())
                    player.States()->ChangeByName("Walk");
                else
                    player.States()->ChangeByName(PlayerComponent::k_IdleStateName);
            }
        }
    };

    NS_STATE(FallPlayerState, NS::Game::Player::PlayerComponent)
} // namespace NS::Game::Player
