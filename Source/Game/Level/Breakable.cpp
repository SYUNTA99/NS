#include "Game/Level/Breakable.h"

#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <algorithm>
#include <cmath>

namespace NS::Game::Level
{
    void Breakable::SetMass(float mass) noexcept
    {
        if (!std::isfinite(mass))
        {
			return;
        }
        // 0 だと勢いを質量で割れなくなる。基準の 1 の 100 分の 1 まで軽くできれば足りる
        m_mass = std::max(mass, 0.01f);
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
