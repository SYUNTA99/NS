#include "Game/Player/PlayerInputRelay.h"

#include "Game/Player/PlayerComponent.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/GameObject.h"

namespace NS::Game::Player
{
    // 入力 (EarlyUpdate) の直後、移動の PlayerComponent (Update) より前に渡す
    PlayerInputRelay::PlayerInputRelay() noexcept
        : NS::Obj::Component(NS::Obj::TickPriority::EarlyUpdate + 10)
    {}

    void PlayerInputRelay::OnStart()
    {
        if (Owner() == nullptr)
        {
            m_input = nullptr;
            m_player = nullptr;
            return;
        }
        m_input = Owner()->FindComponent<NS::Obj::PlayerInput>();
        m_player = Owner()->FindComponent<PlayerComponent>();
    }

    void PlayerInputRelay::OnUpdate()
    {
        if (m_input == nullptr || m_player == nullptr || !m_input->IsActive())
            return;

        m_player->SetDesiredMove(m_input->DesiredDirection(), m_input->DesiredSpeedScale());
        // 掴まりが使うのは camera 回転をかける前の生ローカル入力
        m_player->SetClimbMove(m_input->ClimbRight(), m_input->ClimbForward());
        if (m_input->JumpPressed())
            m_player->SetJumpPressed();
        if (m_input->ReleaseLedgePressed())
        {
            m_player->SetReleaseLedgePressed();
        }
        m_player->SetJumpHeld(m_input->JumpHeld());
    }
} // namespace NS::Game::Player
