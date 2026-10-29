#include "NSlib/CoreObject/Asset/Effect.h"

#include <utility>

namespace NS
{
    Effect::Effect(std::string id) noexcept
    {
        m_resource.id = std::move(id);
    }

    void Effect::Replace(std::string path, Gfx::EffectResourceDesc resource, std::string label) noexcept
    {
        m_path = std::move(path);
        if (label.empty())
        {
            label = resource.name;
        }
        m_label = std::move(label);
        m_resource = std::move(resource);
    }

    void Effect::Invalidate() noexcept
    {
        m_resource.path.clear();
    }
} // namespace NS
