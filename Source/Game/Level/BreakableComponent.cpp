#include "Game/Level/BreakableComponent.h"

#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <cmath>

namespace NS::Game::Level
{
    namespace
    {
        // 0 だと勢いを質量で割れなくなる。基準の 1 の 100 分の 1 まで軽くできれば足りる
        constexpr float k_MinMass = 0.01f;
        // 0 は「最低の勢いでも壊れる」を意味するので許す。負だけ止める
        constexpr float k_MinToughness = 0.0f;
    } // namespace

    void BreakableComponent::SetMass(float mass) noexcept
    {
        if (!std::isfinite(mass))
            return;
        m_mass = mass;
        if (m_mass < k_MinMass)
            m_mass = k_MinMass;
    }

    void BreakableComponent::SetToughness(float toughness) noexcept
    {
        if (!std::isfinite(toughness))
            return;
        m_toughness = toughness;
        if (m_toughness < k_MinToughness)
            m_toughness = k_MinToughness;
    }

    NS_CLASS(BreakableComponent)
} // namespace NS::Game::Level
