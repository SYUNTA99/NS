#pragma once

#include "Runtime/Object/Component.h"
#include "Runtime/Object/StateMachine.h"

namespace NS::Game::Entity
{
    //! @brief 状態機械を持つ Component の抽象基底
    //! @details 所有者の型が付いた NS::Object::StateMachine は派生が持つ。ここにあるのは、所有者の型を
    //! 知らずに状態の型で移す・問う口だけ
    //! TypeRegistry には登録しない。実体化できるのは派生だけ
    //! 依存: NS::Object::Component
    class EntityStateManagerComponent : public NS::Object::Component
    {
    public:
        EntityStateManagerComponent() noexcept;

        //! 現在状態の表示名。未組立は空文字
        [[nodiscard]] virtual const char* CurrentName() const noexcept = 0;
        //! 状態を 1 つでも組めているか
        [[nodiscard]] virtual bool IsBuilt() const noexcept = 0;
        //! OnExit / OnEnter を呼ばずに先頭の状態へ戻す。やり直しで使う
        virtual void ResetToFirst() noexcept = 0;

        //! 状態 TState へ移る。並びに無い状態と未組立は false
        template <typename TState> bool Change() { return ChangeToState(NS::Object::StateIdOf<TState>()); }

        //! 現在状態が TState の場合 true、それ以外の場合は false
        template <typename TState> [[nodiscard]] bool IsCurrent() const noexcept
        {
            return CurrentStateId() == NS::Object::StateIdOf<TState>();
        }

        NS_REFLECT_NONE(EntityStateManagerComponent, NS::Object::Component)

    protected:
        //! 印の状態へ移る。並びに無い状態と未組立は false
        //! @details 外から呼べるのは Change だけにする。StateId は const void*
        //! なので、状態でない番地を渡してもビルドが通る
        virtual bool ChangeToState(NS::Object::StateId id) = 0;

        //! 現在状態の印。未組立は nullptr
        [[nodiscard]] virtual NS::Object::StateId CurrentStateId() const noexcept = 0;
    };
} // namespace NS::Game::Entity
