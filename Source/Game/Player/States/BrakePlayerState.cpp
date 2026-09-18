#include "Game/Player/PlayerComponent.h"
#include "Game/Player/PlayerState.h"

#include "Game/Entity/EntityStateManagerComponent.h"

namespace NS::Game::Player
{
    //! ブレーキ。止まるまで入力の向きへ加速しない。移る先は落下と立ち
    class BrakePlayerState final : public PlayerState
    {
    public:
        static constexpr const char* k_Name = "Brake";

        [[nodiscard]] const char* Name() const noexcept override { return k_Name; }

        void OnStep(PlayerComponent& player, float dt) override
        {
            player.TickTimers(dt);
            player.ApplyBrake(dt);
            player.Jump(dt);
            player.CutJumpRelease();
            player.Gravity(dt);
            player.Move(dt);
            player.SyncGroundState();

            if (player.ShouldFall())
            {
                player.States()->ChangeByName("Fall");
            }
            else if (player.IsStopped())
            {
                player.States()->ChangeByName(PlayerComponent::k_IdleStateName);
            }
        }
    };

    NS_STATE(BrakePlayerState, NS::Game::Player::PlayerComponent)
} // namespace NS::Game::Player
