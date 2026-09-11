#include "Game/Player/PlayerComponent.h"
#include "Game/Player/PlayerState.h"

namespace NS::Game::Player
{
    //! よじ登り。登り切った歩に立ちへ移る
    class LedgeClimbingPlayerState final : public PlayerState
    {
    public:
        // ClimbLedge が移す先と同じ綴り。別々に書くと ChangeByName が false を返し、登り始めても状態が移らない
        static constexpr const char* k_Name = PlayerComponent::k_LedgeClimbingStateName;

        [[nodiscard]] const char* Name() const noexcept override { return k_Name; }

        void OnStep(PlayerComponent& player, float dt) override { player.UpdateLedgeClimb(dt); }
    };

    NS_STATE(LedgeClimbingPlayerState, NS::Game::Player::PlayerComponent)
} // namespace NS::Game::Player
