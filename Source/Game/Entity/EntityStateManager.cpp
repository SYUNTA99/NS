#include "Game/Entity/EntityStateManager.h"

namespace NS::Game::Entity
{
    EntityStateManager::EntityStateManager() noexcept
        : NS::Obj::Component(NS::Obj::TickPriority::Update)
    {}
} // namespace NS::Game::Entity
