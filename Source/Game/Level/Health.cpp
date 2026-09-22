#include "Game/Level/Health.h"

#include "Runtime/Object/Reflection/TypeRegistry.h"

namespace NS::Game::Level
{
    Health::Health() noexcept = default;

    void Health::ApplyDamage(int amount) noexcept
    {
        if (m_current <= 0 || amount <= 0)
        {
            return;
        }


        m_current -= amount;
        if (m_current < 0)
        {
            m_current = 0;
        }
    }

    NS_CLASS(Health)
} // namespace NS::Game::Level
