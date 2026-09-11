#include "Game/Player/PlayerComponent.h"
#include "Game/Player/PlayerState.h"

namespace NS::Game::Player
{
    //! ぶら下がり。移る先はよじ登りと立ち
    class LedgeHangingPlayerState final : public PlayerState
    {
    public:
        // LedgeGrab が移す先と同じ綴り。別々に書くと ChangeByName が false を返し、掴んでも状態が移らない
        static constexpr const char* k_Name = PlayerComponent::k_LedgeHangingStateName;

        [[nodiscard]] const char* Name() const noexcept override { return k_Name; }

        void OnStep(PlayerComponent& player, float dt) override
        {
            player.HoldLedge(dt);
            if (player.ShouldClimbLedge())
            {
                player.ClimbLedge();
                return;
            }
            if (player.ShouldDropLedge())
            {
                player.DropLedge();
                return;
            }
            player.Shimmy(dt);
        }
    };

    NS_STATE(LedgeHangingPlayerState, NS::Game::Player::PlayerComponent)
} // namespace NS::Game::Player
