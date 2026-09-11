#pragma once

#include "Runtime/Object/Component.h"

#include <string>
#include <string_view>
#include <vector>

namespace NS::Game::Entity
{
    //! @brief 状態機械を持つ Component の抽象基底
    //! @details 型の付いた NS::Object::StateMachine は派生が持つ。ここにあるのは、状態の型を
    //! 知らない側から現在状態を問う仮想関数と、状態一覧の文字列を割る処理だけ
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
        //! 登録名の状態へ移る。未知名と未組立は false
        virtual bool ChangeByName(std::string_view name) = 0;
        //! OnExit / OnEnter を呼ばずに先頭の状態へ戻す。やり直しで使う
        virtual void ResetToFirst() noexcept = 0;

        //! 現在状態が引数の登録名か
        [[nodiscard]] bool IsCurrent(std::string_view name) const noexcept;

        NS_REFLECT_NONE(EntityStateManagerComponent, NS::Object::Component)

    protected:
        //! セミコロン区切りの一覧を登録名へ割る。空要素は捨てる
        [[nodiscard]] static std::vector<std::string> SplitStateNames(const std::string& list);
    };
} // namespace NS::Game::Entity
