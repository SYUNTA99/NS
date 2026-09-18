#include "Game/Player/PlayerComponent.h"
#include "Game/Player/PlayerState.h"

#include "Game/Entity/EntityStateManagerComponent.h"

namespace NS::Game::Player
{
    //! 立ち。移る先は落下と走り
    class IdlePlayerState final : public PlayerState
    {
    public:
        // 立ちへ移す所と同じ綴り。別々に書くと片方を直した時に ChangeByName が false を返し、
        // よじ登り・落下・走り・ブレーキから立ちへ移れなくなる
        static constexpr const char* k_Name = PlayerComponent::k_IdleStateName;

        [[nodiscard]] const char* Name() const noexcept override { return k_Name; }

        void OnStep(PlayerComponent& player, float dt) override
        {
            player.TickTimers(dt);
            player.ApplyFriction(dt);
            player.Jump(dt);
            player.CutJumpRelease();
            player.Gravity(dt);
            player.Move(dt);
            player.SyncGroundState();

            if (player.ShouldFall())
                player.States()->ChangeByName("Fall");
            else if (player.ShouldWalk())
                player.States()->ChangeByName("Walk");
        }
    };

    NS_STATE(IdlePlayerState, NS::Game::Player::PlayerComponent)
} // namespace NS::Game::Player
