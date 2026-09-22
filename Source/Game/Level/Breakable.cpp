#include "Game/Level/Breakable.h"

#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <cmath>

namespace NS::Game::Level
{
    namespace
    {
        // 0 だと勢いを質量で割れなくなる。基準の 1 の 100 分の 1 まで軽くできれば足りる
        constexpr float k_MinMass = 0.01f;
    } // namespace

    void Breakable::SetMass(float mass) noexcept
    {
        if (!std::isfinite(mass))
        {
			return;
        }
        m_mass = mass;
        if (m_mass < k_MinMass)
        {
            m_mass = k_MinMass;
        }
    }

    void Breakable::SetToughness(float toughness) noexcept
    {
        if (!std::isfinite(toughness))
        {
            return;
        }
        m_toughness = toughness;
    }

    NS_CLASS(Breakable)
} // namespace NS::Game::Level
