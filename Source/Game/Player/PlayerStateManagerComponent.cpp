#include "Game/Player/PlayerStateManagerComponent.h"

#include "Game/Player/PlayerComponent.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <vector>

namespace NS::Game::Player
{
    const char* PlayerStateManagerComponent::CurrentName() const noexcept
    {
        return m_machine.CurrentName();
    }

    bool PlayerStateManagerComponent::IsBuilt() const noexcept
    {
        return m_machine.IsBuilt();
    }

    bool PlayerStateManagerComponent::ChangeByName(std::string_view name)
    {
        if (m_player == nullptr)
            return false;
        return m_machine.Change(*m_player, name);
    }

    void PlayerStateManagerComponent::ResetToFirst() noexcept
    {
        m_machine.Reset();
    }

    void PlayerStateManagerComponent::EnsureBuilt(PlayerComponent& player)
    {
        // 状態が呼ぶ ChangeByName は 1 歩の中で起きる。渡された所有者をここで控えると、
        // OnStart を通らない検証台でも遷移が false を返さない
        m_player = &player;
        if (!m_machine.IsBuilt())
            BuildStates(player);
    }

    void PlayerStateManagerComponent::Step(PlayerComponent& player, float dt)
    {
        EnsureBuilt(player);
        m_machine.Step(player, dt);
    }

    void PlayerStateManagerComponent::OnStart()
    {
        NS::Game::Entity::EntityStateManagerComponent::OnStart();

        if (Owner() == nullptr)
            return;

        m_player = Owner()->FindComponent<PlayerComponent>();
    }

    void PlayerStateManagerComponent::BuildStates(PlayerComponent& player)
    {
        const std::vector<std::string> names = SplitStateNames(m_stateNames);

        if (m_machine.Build(player, names))
            return;
        if (m_machine.IsBuilt())
        {
            NS_LOG_ERROR(Game, "PlayerStateManager: 状態一覧に未登録名があり飛ばした: {}", m_stateNames);
            return;
        }

        // 退避の並びも同じ登録簿から作る。登録ごとリンカに落とされた時はここでも組めないので、
        // その検知は player_state_registration_test に任せる
        NS_LOG_ERROR(Game, "PlayerStateManager: 状態一覧が組めないため既定の並びへ退避: {}", m_stateNames);
        const std::vector<std::string> fallback = {PlayerComponent::k_IdleStateName,
                                                   "Walk",
                                                   "Fall",
                                                   PlayerComponent::k_LedgeHangingStateName,
                                                   PlayerComponent::k_LedgeClimbingStateName,
                                                   PlayerComponent::k_BodySlamStateName};
        m_machine.Build(player, fallback);
    }

    NS_CLASS(PlayerStateManagerComponent)
} // namespace NS::Game::Player
