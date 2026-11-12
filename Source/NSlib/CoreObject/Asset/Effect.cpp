#include "NSlib/CoreObject/Asset/Effect.h"

#include "NSlib/Gfx/D3DEffect.h"

#include <utility>

namespace NS
{
    Effect::Effect(std::string path) noexcept : m_path(std::move(path)) {}

    Effect::Effect(std::string path, std::unique_ptr<Gfx::D3DEffect> effect) noexcept
        : m_path(std::move(path)), m_effect(std::move(effect))
    {
    }

    Effect::~Effect() = default;
} // namespace NS
