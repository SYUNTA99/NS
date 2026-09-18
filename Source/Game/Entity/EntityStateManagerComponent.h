#pragma once

#include "Runtime/Object/Component.h"

#include <string>
#include <string_view>
#include <vector>

namespace NS::Game::Entity
{
    //! @brief 状態機械を持つ Component の抽象基底
    //! @details 所有者の型が付いた NS::Object::StateMachine は派生が持つ。ここにあるのは、所有者の型を
    //! 知らずに状態の型で移す・問う口と、状態一覧の文字列を割る処理だけ
    //! TypeRegistry には登録しない。実体化できるのは派生だけ
    //! 依存: NS::Object::Component
    class EntityStateManagerComponent : public NS::Object::Component
    {
    public:
        EntityStateManagerComponent() noexcept;

        //! 現在状態の登録名。未組立は空文字
        [[nodiscard]] virtual const char* CurrentName() const noexcept = 0;
        //! 状態を 1 つでも組めているか
        [[nodiscard]] virtual bool IsBuilt() const noexcept = 0;
        //! OnExit / OnEnter を呼ばずに先頭の状態へ戻す。やり直しで使う
        virtual void ResetToFirst() noexcept = 0;

        //! 状態 TState へ移る。一覧に無い状態と未組立は false
        template <typename TState> bool Change() { return ChangeByName(TState::k_Name); }

        //! 現在状態が TState の場合 true、それ以外の場合は false
        template <typename TState> [[nodiscard]] bool IsCurrent() const noexcept
        {
            return std::string_view(CurrentName()) == TState::k_Name;
        }

        NS_REFLECT_NONE(EntityStateManagerComponent, NS::Object::Component)

    protected:
        //! 登録名の状態へ移る。未知名と未組立は false
        //! @details 外から呼べるのは Change だけにする。
        //! 名前で移せると、綴りを間違えてもビルドが通り、状態が移らないだけになる
        virtual bool ChangeByName(std::string_view name) = 0;

        //! セミコロン区切りの一覧を登録名へ割る。空要素は捨てる
        [[nodiscard]] static std::vector<std::string> SplitStateNames(const std::string& list);
    };
} // namespace NS::Game::Entity
