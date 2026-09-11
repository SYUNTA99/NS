#include "Game/Player/PlayerInputRelayComponent.h"

#include "Game/Player/PlayerComponent.h"
#include "Runtime/Object/Components/PlayerInputComponent.h"
#include "Runtime/Object/GameObject.h"

namespace NS::Game::Player
{
    // 入力 (EarlyUpdate) の直後、移動の PlayerComponent (Update) より前に渡す
    PlayerInputRelayComponent::PlayerInputRelayComponent() noexcept
        : NS::Object::Component(NS::Object::TickPriority::EarlyUpdate + 10)
    {}

    void PlayerInputRelayComponent::OnStart()
    {
        if (Owner() == nullptr)
        {
            m_input = nullptr;
            m_player = nullptr;
            return;
        }
        m_input = Owner()->FindComponent<NS::Object::PlayerInputComponent>();
        m_player = Owner()->FindComponent<PlayerComponent>();
    }

    void PlayerInputRelayComponent::OnUpdate()
    {
        if (m_input == nullptr || m_player == nullptr || !m_input->IsActive())
            return;

        m_player->SetDesiredMove(m_input->DesiredDirection(), m_input->DesiredSpeedScale());
        // 掴まりが使うのは camera 回転をかける前の生ローカル入力
        m_player->SetClimbMove(m_input->ClimbRight(), m_input->ClimbForward());
        if (m_input->JumpPressed())
            m_player->SetJumpPressed();
        m_player->SetJumpHeld(m_input->JumpHeld());
    }
} // namespace NS::Game::Player
