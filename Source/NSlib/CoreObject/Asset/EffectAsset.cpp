#include "NSlib/CoreObject/Asset/EffectAsset.h"

#include <utility>

namespace NS
{
    EffectAsset::EffectAsset(std::string id) noexcept
    {
        m_resource.id = std::move(id);
    }

    void EffectAsset::Replace(std::string path, Gfx::EffectResourceDesc resource, std::string label) noexcept
    {
        m_path = std::move(path);
        if (label.empty())
        {
            label = resource.name;
        }
        m_label = std::move(label);
        m_resource = std::move(resource);
    }

    void EffectAsset::Invalidate() noexcept
    {
        m_resource.path.clear();
    }
} // namespace NS
