#pragma once

#include "Runtime/Object/Component.h"

#include <cstddef>

namespace NS::Game::Entity
{
    //! @brief 調整値の組を添字で切り替える Component の抽象基底
    //! @details 基底が持つのは今どの組かの添字だけ。組の実体は派生が型付きの配列で持つ
    //! 強化・変身で調整値をまとめて差し替える遊びのために置いてある。現在の使い手は組を 1 つしか持たない
    //! TypeRegistry には登録しない。実体化できるのは派生だけ
    //! 依存: NS::Object::Component
    class EntityStatsManagerComponent : public NS::Object::Component
    {
    public:
        EntityStatsManagerComponent() noexcept;

        [[nodiscard]] std::size_t CurrentIndex() const noexcept { return m_index; } //!< 現在の組の添字

        //! 組を切り替える。範囲外の添字は何もせず false
        bool Change(std::size_t index) noexcept;

        //! 派生が持つ組の数。Change の範囲判定がこれを見る
        [[nodiscard]] virtual std::size_t StatsCount() const noexcept = 0;

        // 抽象基底なので TypeRegistry には登録せず、リフレクションの鎖だけ通す
        NS_REFLECT_NONE(EntityStatsManagerComponent, NS::Object::Component)

    protected:
        //! 添字が実際に動いた後に呼ぶ。既定は何もしない
        //! noexcept にしてあるのは Change が noexcept で、投げると即終了になるため
        virtual void OnStatsChanged() noexcept {}

    private:
        std::size_t m_index = 0;
    };
} // namespace NS::Game::Entity
