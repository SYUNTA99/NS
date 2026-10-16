#include "NSlib/CoreObject/Asset/Material.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/Asset/Shader.h"
#include "NSlib/CoreObject/Asset/Texture.h"
#include "NSlib/Graphics/D3DMaterial.h"
#include "NSlib/Graphics/FrameConstants.h"

#include "NSlib/Core/Types.h"
#include <utility>

namespace NS
{
    std::optional<Gfx::BlendMode> ParseBlend(std::string_view name) noexcept
    {
        if (name == "Opaque")
        {
            return Gfx::BlendMode::Opaque;
        }
        if (name == "Alpha")
        {
            return Gfx::BlendMode::Alpha;
        }
        if (name == "Additive")
        {
            return Gfx::BlendMode::Additive;
        }
        return std::nullopt;
    }

    std::string_view BlendName(Gfx::BlendMode blend) noexcept
    {
        switch (blend)
        {
        case Gfx::BlendMode::Alpha:
            return "Alpha";
        case Gfx::BlendMode::Additive:
            return "Additive";
        case Gfx::BlendMode::Opaque:
            break;
        }
        return "Opaque";
    }

    Material::Material(std::string path,
                       MaterialFileDesc file,
                       Shader* vertexShader,
                       Shader* pixelShader,
                       std::span<Texture* const> textures)
        : m_path(std::move(path)), m_file(std::move(file)), m_vertexShader(vertexShader), m_pixelShader(pixelShader)
    {
        NS_ASSERT(Graphics, vertexShader != nullptr && pixelShader != nullptr, "マテリアルにシェーダーが無い");

        // 定数バッファは slot 0 で Model が流す FrameCB に合わせる
        Gfx::D3DMaterialDesc desc{};
        desc.vertexShader = m_vertexShader->D3D();
        desc.pixelShader = m_pixelShader->D3D();
        desc.constantBufferSize = sizeof(Gfx::FrameCB);
        desc.cbSlot = 0;
        desc.blend = m_file.blend;
        m_material = Gfx::D3DMaterial::Create(desc);

        for (std::size_t i = 0; i < textures.size(); ++i)
        {
            m_material->SetTexture(static_cast<uint>(i), textures[i]->D3D());
        }
    }

    Material::Material(std::string path) : m_path(std::move(path)) {}

    Material::~Material() noexcept = default;

    const std::string& Material::Path() const noexcept
    {
        return m_path;
    }

    bool Material::IsEmpty() const noexcept
    {
        return m_material == nullptr;
    }

    Gfx::D3DMaterial* Material::D3D() const noexcept
    {
        if (m_material != nullptr)
        {
            return m_material.get();
        }
        if (m_standIn != nullptr)
        {
            return m_standIn->D3D();
        }
        return nullptr;
    }

    void Material::SetStandIn(const Material* standIn) noexcept
    {
        NS_ASSERT(Graphics, standIn != this, "マテリアルの代わりに自分を指そうとしている");
        m_standIn = standIn;
    }

    Shader* Material::VertexShader() const noexcept
    {
        return m_vertexShader;
    }

    Shader* Material::PixelShader() const noexcept
    {
        return m_pixelShader;
    }

    const MaterialFileDesc& Material::File() const noexcept
    {
        return m_file;
    }

    Color Material::BaseColor() const noexcept
    {
        return Color{m_file.baseColor};
    }

    void Material::SetBaseColor(const Color& color) noexcept
    {
        m_file.baseColor = color.ToVector3();
    }

    std::string Material::BlendText() const
    {
        return std::string{BlendName(m_file.blend)};
    }

    void Material::SetBlendText(const std::string& text)
    {
        const std::optional<Gfx::BlendMode> blend = ParseBlend(text);
        if (!blend)
        {
            NS_LOG_WARN(Graphics, "マテリアルの混ぜ方 {} は Opaque・Alpha・Additive のどれでもないので受けない", text);
            return;
        }
        m_file.blend = *blend;
    }

    const std::string& Material::VertexShaderPath() const noexcept
    {
        return m_file.vertexShader;
    }

    void Material::SetVertexShaderPath(const std::string& path)
    {
        m_file.vertexShader = path;
    }

    const std::string& Material::PixelShaderPath() const noexcept
    {
        return m_file.pixelShader;
    }

    void Material::SetPixelShaderPath(const std::string& path)
    {
        m_file.pixelShader = path;
    }

    std::string Material::TexturePath(std::size_t slot) const
    {
        if (slot >= m_file.textures.size())
        {
            return {};
        }
        return m_file.textures[slot];
    }

    void Material::SetTexturePathAt(std::size_t slot, const std::string& path)
    {
        if (slot >= m_file.textures.size())
        {
            // 道の無い枠へ空を書いても、枠の数は増やさない
            if (path.empty())
            {
                return;
            }
            m_file.textures.resize(slot + 1);
        }
        m_file.textures[slot] = path;
    }
} // namespace NS
