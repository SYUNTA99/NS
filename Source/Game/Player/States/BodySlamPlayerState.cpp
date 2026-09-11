#include "Game/Player/PlayerComponent.h"
#include "Game/Player/PlayerState.h"

namespace NS::Game::Player
{
    //! 突進。距離を使い切るか進めない歩が続いた時に立ちへ移る
    class BodySlamPlayerState final : public PlayerState
    {
    public:
        // BodySlam が移す先と同じ綴り。別々に書くと ChangeByName が false を返し、発動しても突進の状態にならない
        static constexpr const char* k_Name = PlayerComponent::k_BodySlamStateName;

        [[nodiscard]] const char* Name() const noexcept override { return k_Name; }

        void OnStep(PlayerComponent& player, float dt) override { player.UpdateBodySlam(dt); }
    };

    NS_STATE(BodySlamPlayerState, NS::Game::Player::PlayerComponent)
} // namespace NS::Game::Player
