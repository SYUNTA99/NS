#pragma once

#include "NSlib/CoreObject/Object.h"
#include "NSlib/Gfx/Render/EffectTypes.h"

#include <string>

namespace NS
{
    class Effect : public Object
    {
    public:
        explicit Effect(std::string id) noexcept;
        const std::string& Id() const noexcept { return m_resource.id; }
        const std::string& Path() const noexcept { return m_path; }
        const std::string& Label() const noexcept { return m_label; }
        bool IsValid() const noexcept { return !m_resource.path.empty(); }
        const Gfx::EffectResourceDesc& Resource() const noexcept { return m_resource; }
        void Replace(std::string path, Gfx::EffectResourceDesc resource, std::string label = {}) noexcept;
        void Invalidate() noexcept;

        NS_REFLECT_NONE(Effect, Object)

    private:
        std::string m_path;
        std::string m_label;
        Gfx::EffectResourceDesc m_resource;
    };
} // namespace NS
