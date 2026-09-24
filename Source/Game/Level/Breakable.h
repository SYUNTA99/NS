#pragma once

#include "Runtime/Object/Component.h"

namespace NS::Game::Level
{
    //! @brief ぶつかられる側が持つ耐久
    //! @details 値を持つだけで衝突の分岐はしない。重さは同じ配置物の RigidBody の質量を使う
    class Breakable : public NS::Obj::Component
    {
    public:
        //! 破壊できるかの閾値。勢いがこれ以上なら壊れる。下限 0
        [[nodiscard]] float Toughness() const noexcept { return m_toughness; }
        //! 耐久を置く。非有限値は捨て、負は 0 へ丸める
        void SetToughness(float toughness) noexcept;

        // 同じ壁が勢い次第で壊す対象にも跳ね返される壁にもなる。個体ごとに Inspector で入れる
        NS_REFLECT_BEGIN(Breakable, NS::Obj::Component)
        NS_REFLECT_ACCESSOR(float, "耐久", Toughness(), SetToughness)
        NS_REFLECT_END()

    private:
        float m_toughness = 1.0f; // 破壊できるかの閾値
    };
} // namespace NS::Game::Level
