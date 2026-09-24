#include "Game/Player/PlayerStateManager.h"

#include "Game/Player/PlayerComponent.h"
#include "Game/Player/States/BodySlamPlayerState.h"
#include "Game/Player/States/BrakePlayerState.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/LedgeClimbingPlayerState.h"
#include "Game/Player/States/LedgeHangingPlayerState.h"
#include "Game/Player/States/WalkPlayerState.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

namespace NS::Game::Player
{
    bool PlayerStateManager::IsBuilt() const noexcept
    {
        return m_machine.IsBuilt();
    }

    bool PlayerStateManager::ChangeToState(NS::Obj::StateId id)
    {
        if (m_player == nullptr)
        {
            return false;
        }
        return m_machine.Change(*m_player, id);
    }

    NS::Obj::StateId PlayerStateManager::CurrentStateId() const noexcept
    {
        return m_machine.CurrentId();
    }

    void PlayerStateManager::ResetToFirst() noexcept
    {
        m_machine.Reset();
    }

    void PlayerStateManager::EnsureBuilt(PlayerComponent& player)
    {
        // 状態が呼ぶ遷移は 1 フレームの中で起きる。渡された所有者をここで控えると、
        // OnStart を通らない検証台でも遷移が false を返さない
        m_player = &player;
        if (!m_machine.IsBuilt())
            BuildStates(player);
    }

    void PlayerStateManager::Step(PlayerComponent& player, float dt)
    {
        EnsureBuilt(player);
        m_machine.Step(player, dt);
    }

    void PlayerStateManager::OnStart()
    {
        NS::Game::Entity::EntityStateManager::OnStart();

        if (Owner() == nullptr)
            return;

        m_player = Owner()->FindComponent<PlayerComponent>();
    }

    void PlayerStateManager::BuildStates(PlayerComponent& player)
    {
        m_machine.Build<IdlePlayerState,
                        WalkPlayerState,
                        FallPlayerState,
                        LedgeHangingPlayerState,
                        LedgeClimbingPlayerState,
                        BodySlamPlayerState,
                        BrakePlayerState>(player);
    }

    NS_CLASS(PlayerStateManager)
} // namespace NS::Game::Player
