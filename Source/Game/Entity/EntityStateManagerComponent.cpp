#include "Game/Entity/EntityStateManagerComponent.h"

namespace NS::Game::Entity
{
    EntityStateManagerComponent::EntityStateManagerComponent() noexcept
        : NS::Obj::Component(NS::Obj::TickPriority::Update)
    {}
} // namespace NS::Game::Entity
