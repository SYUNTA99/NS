#include "NSlib/CoreObject/Asset/Texture.h"

#include "NSlib/Graphics/D3DTexture.h"

namespace NS
{
    Texture::Texture(const std::string& path) : m_texture(Gfx::D3DTexture::Create({.path = path})) {}

    Texture::~Texture() noexcept = default;

    Gfx::D3DTexture* Texture::D3D() const noexcept
    {
        return m_texture.get();
    }
} // namespace NS
