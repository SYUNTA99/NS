#include "NSlib/CoreObject/Asset/Texture.h"

#include "NSlib/Gfx/D3DTexture.h"

namespace NS
{
    Texture::Texture(std::string path) : m_path(std::move(path)), m_texture(Gfx::D3DTexture::Create({.path = m_path}))
    {
    }

    Texture::~Texture() noexcept = default;

    const std::string& Texture::Path() const noexcept { return m_path; }

    Gfx::D3DTexture* Texture::D3D() const noexcept { return m_texture.get(); }
} // namespace NS
