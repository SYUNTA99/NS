#include "Game/Level/Health.h"

namespace NS::Game::Level
{
    void Health::SetMaxHealth(int value) noexcept
    {
        if (value > 0)
        {
            m_maxHealth = value;
            if (m_current > value)
            {
                m_current = value;
            }
        }
    }

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

} // namespace NS::Game::Level
