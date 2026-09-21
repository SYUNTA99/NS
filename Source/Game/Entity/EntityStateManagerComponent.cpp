#include "Game/Entity/EntityStateManagerComponent.h"

namespace NS::Game::Entity
{
    EntityStateManagerComponent::EntityStateManagerComponent() noexcept
        : NS::Object::Component(NS::Object::TickPriority::Update)
    {}
} // namespace NS::Game::Entity
