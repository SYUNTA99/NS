#include "Game/Entity/EntityStateManagerComponent.h"

#include <cstddef>

namespace NS::Game::Entity
{
    EntityStateManagerComponent::EntityStateManagerComponent() noexcept
        : NS::Object::Component(NS::Object::TickPriority::Update)
    {}

    bool EntityStateManagerComponent::IsCurrent(std::string_view name) const noexcept
    {
        return std::string_view(CurrentName()) == name;
    }

    std::vector<std::string> EntityStateManagerComponent::SplitStateNames(const std::string& list)
    {
        std::vector<std::string> names;
        std::size_t pos = 0;
        while (pos < list.size())
        {
            std::size_t end = list.find(';', pos);
            if (end == std::string::npos)
                end = list.size();
            if (end > pos)
                names.push_back(list.substr(pos, end - pos));
            pos = end + 1;
        }
        return names;
    }
} // namespace NS::Game::Entity
