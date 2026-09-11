#pragma once

#include "Runtime/Object/Component.h"

namespace NS::Game::Level
{
    //! @brief ぶつかられる側が持つ質量と耐久
    //! @details 値を持つだけで衝突の分岐はしない
    class BreakableComponent : public NS::Object::Component
    {
    public:
        //! 押し飛ばされる時の重さ。大きいほど飛ばない。下限 0.01
        [[nodiscard]] float Mass() const noexcept { return m_mass; }
        //! 質量を置く。非有限値は捨て、0.01 未満は 0.01 へ丸める
        void SetMass(float mass) noexcept;

        //! 破壊できるかの閾値。勢いがこれ以上なら壊れる。下限 0
        [[nodiscard]] float Toughness() const noexcept { return m_toughness; }
        //! 耐久を置く。非有限値は捨て、負は 0 へ丸める
        void SetToughness(float toughness) noexcept;

        // 同じ壁が勢い次第で壊す対象にも跳ね返される壁にもなる。個体ごとに Inspector で入れる
        NS_REFLECT_BEGIN(BreakableComponent, NS::Object::Component)
        NS_REFLECT_ACCESSOR(float, "質量", Mass(), SetMass)
        NS_REFLECT_ACCESSOR(float, "耐久", Toughness(), SetToughness)
        NS_REFLECT_END()

    private:
        float m_mass = 1.0f;      // 押し飛ばされる時の重さ。基準の 1 個
        float m_toughness = 1.0f; // 破壊できるかの閾値
    };
} // namespace NS::Game::Level
