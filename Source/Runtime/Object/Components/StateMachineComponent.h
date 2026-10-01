#pragma once

#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Reflection/Reflection.h"
#include "Runtime/Object/StateMachine.h"

namespace NS::Obj
{
    //! @brief 持ち主の Actor を所有者にした状態機械を持つ部品
    //! @details Actor::Update が OnUpdate を呼び、組めていれば 1 固定ステップ進める
    class StateMachineComponent : public Component
    {
    public:
        //! @brief 状態の型の並びから持ち主の状態機械を組み、先頭へ入る。持ち主が居なければ何もしない
        template <typename... TStates> void Build()
        {
            Actor* owner = Owner();
            if (owner != nullptr)
            {
                m_machine.template Build<TStates...>(*owner);
            }
        }

        [[nodiscard]] StateMachine<Actor>& Machine() noexcept { return m_machine; }
        [[nodiscard]] const StateMachine<Actor>& Machine() const noexcept { return m_machine; }

        //! 持ち主が居て状態機械を組めていれば、今の状態で 1 固定ステップ進める
        void OnUpdate() override;

        NS_REFLECT_NONE(StateMachineComponent, Component)

    private:
        StateMachine<Actor> m_machine;
    };
} // namespace NS::Obj
