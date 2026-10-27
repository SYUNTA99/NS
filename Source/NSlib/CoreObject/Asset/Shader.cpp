#include "NSlib/CoreObject/Asset/Shader.h"

#include "NSlib/Gfx/D3DShader.h"

#include <utility>

namespace NS
{
    Shader::Shader(std::string path) : m_path(std::move(path)), m_shader(Gfx::D3DShader::Create(m_path)) {}

    Shader::~Shader() noexcept = default;

    const std::string& Shader::Path() const noexcept { return m_path; }

    Gfx::D3DShader* Shader::D3D() const noexcept { return m_shader.get(); }

    bool Shader::Reload() { return m_shader->Reload(m_path); }
} // namespace NS
