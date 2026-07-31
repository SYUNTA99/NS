#include "Game/Level/HealthComponent.h"

#include "Runtime/Object/Reflection/TypeRegistry.h"

namespace NS::Game::Level
{
    HealthComponent::HealthComponent() noexcept = default;

    void HealthComponent::ApplyDamage(int amount) noexcept
    {
        if (m_current <= 0 || amount <= 0)
            return;

        m_current -= amount;
        if (m_current < 0)
            m_current = 0;
    }

    NS_CLASS(HealthComponent)
} // namespace NS::Game::Level
