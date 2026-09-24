#pragma once

#include "Game/Entity/EntityStateManager.h"
#include "Runtime/Object/Reflection/Reflection.h"
#include "Runtime/Object/StateMachine.h"

namespace NS::Game::Player
{
    class PlayerComponent;

    //! @brief 自機の状態機械を持つ Component
    //! @details 状態の並びは BuildStates にコードで書き、先頭の立ちが初期状態になる
    //! 依存: NS::Game::Entity::EntityStateManager, NS::Obj::StateMachine, PlayerComponent
    class PlayerStateManager : public NS::Game::Entity::EntityStateManager
    {
    public:
        [[nodiscard]] bool IsBuilt() const noexcept override;
        void ResetToFirst() noexcept override;

        //! まだ組んでいなければ組む。遷移に渡す所有者もここで控える
        //! @details Step を待たずに組む必要があるのは、現在状態を見て決める判断が Step より前にあるため
        //! @param[in] player 状態へ渡す所有者
        void EnsureBuilt(PlayerComponent& player);

        //! 現在状態で 1 フレーム進める。初回はここで組む
        //! @param[in] player 状態へ渡す所有者
        //! @param[in] dt 固定ステップの秒数
        void Step(PlayerComponent& player, float dt);

        //! 同居する PlayerComponent を控える。ChangeToState が遷移に所有者を渡すため
        void OnStart() override;

        NS_REFLECT_NONE(PlayerStateManager, NS::Game::Entity::EntityStateManager)

    private:
        bool ChangeToState(NS::Obj::StateId id) override;

        [[nodiscard]] NS::Obj::StateId CurrentStateId() const noexcept override;

        //! 自機の 7 状態を並べて状態機械を組む。並べた型がそのまま移れる状態の全部になる
        void BuildStates(PlayerComponent& player);

        NS::Obj::StateMachine<PlayerComponent> m_machine;
        PlayerComponent* m_player = nullptr; // 遷移に渡す所有者 (非所有)
    };
} // namespace NS::Game::Player
