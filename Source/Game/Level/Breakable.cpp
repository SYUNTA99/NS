#include "Game/Level/Breakable.h"

#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <cmath>

namespace NS::Game::Level
{
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
