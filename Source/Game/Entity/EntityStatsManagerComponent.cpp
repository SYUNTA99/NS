#include "Game/Entity/EntityStatsManagerComponent.h"

namespace NS::Game::Entity
{
    EntityStatsManagerComponent::EntityStatsManagerComponent() noexcept
        : NS::Object::Component(NS::Object::TickPriority::Update)
    {}

    bool EntityStatsManagerComponent::Change(std::size_t index) noexcept
    {
        if (index >= StatsCount())
            return false;

        if (index != m_index)
        {
            m_index = index;
            OnStatsChanged();
        }
        return true;
    }
} // namespace NS::Game::Entity
