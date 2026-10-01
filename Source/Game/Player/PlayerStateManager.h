#pragma once

#include "Runtime/Object/StateMachine.h"

class Player;

namespace NS::Game::Player
{
    class PlayerComponent;

    //! @brief Player Actor が値で所有する自機の状態機械
    //! @details 状態の並びは BuildStates にコードで書き、先頭の立ちが初期状態になる
    class PlayerStateManager
    {
    public:
        //! 状態機械を組めている場合 true、それ以外の場合は false
        [[nodiscard]] bool IsBuilt() const noexcept;
        //! OnExit / OnEnter を呼ばずに先頭の立ちへ戻す
        void ResetToFirst() noexcept;

        //! まだ組んでいなければ組む。遷移に渡す所有者もここで控える
        //! @details Step を待たずに組む必要があるのは、現在状態を見て決める判断が Step より前にあるため
        //! @param[in] player 状態へ渡す所有者
        void EnsureBuilt(::Player& player);

        //! 現在状態で 1 フレーム進める。初回はここで組む
        //! @param[in] player 状態へ渡す所有者
        //! @param[in] dt 固定ステップの秒数
        void Step(::Player& player, float dt);

        [[nodiscard]] NS::Obj::StateMachine<::Player>& Machine() noexcept { return m_machine; }
        [[nodiscard]] const NS::Obj::StateMachine<::Player>& Machine() const noexcept { return m_machine; }

        template <typename TState> bool Change() { return ChangeToState(NS::Obj::StateIdOf<TState>()); }
        template <typename TState> [[nodiscard]] bool IsCurrent() const noexcept
        {
            return CurrentStateId() == NS::Obj::StateIdOf<TState>();
        }

    private:
        bool ChangeToState(NS::Obj::StateId id);

        [[nodiscard]] NS::Obj::StateId CurrentStateId() const noexcept;

        //! 自機の 8 状態を並べて状態機械を組む。並べた型がそのまま移れる状態の全部になる
        void BuildStates(::Player& player);

        NS::Obj::StateMachine<::Player> m_machine;
        ::Player* m_player = nullptr;
    };
} // namespace NS::Game::Player
