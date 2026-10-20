#include "NSlib/CoreObject/Asset/Material.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/Asset/Shader.h"
#include "NSlib/CoreObject/Asset/Texture.h"
#include "NSlib/Gfx/D3DMaterial.h"
#include "NSlib/Gfx/Render/FrameConstants.h"

#include "NSlib/Core/Types.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
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

    namespace
    {
        [[nodiscard]] std::size_t ComponentCount(Gfx::ShaderParamType type) noexcept
        {
            switch (type)
            {
            case Gfx::ShaderParamType::Float:
            case Gfx::ShaderParamType::Int:
            case Gfx::ShaderParamType::Uint:
            case Gfx::ShaderParamType::Bool:
                return 1;
            case Gfx::ShaderParamType::Float2:
                return 2;
            case Gfx::ShaderParamType::Float3:
                return 3;
            case Gfx::ShaderParamType::Float4:
                return 4;
            case Gfx::ShaderParamType::Unsupported:
                break;
            }
            return 0;
        }

        [[nodiscard]] bool IsFloatType(Gfx::ShaderParamType type) noexcept
        {
            return type == Gfx::ShaderParamType::Float || type == Gfx::ShaderParamType::Float2 ||
                   type == Gfx::ShaderParamType::Float3 || type == Gfx::ShaderParamType::Float4;
        }

        [[nodiscard]] bool FitsNumber(Gfx::ShaderParamType type, double number) noexcept
        {
            if (!std::isfinite(number))
            {
                return false;
            }
            if (IsFloatType(type))
            {
                return std::abs(number) <= static_cast<double>(std::numeric_limits<float>::max());
            }
            if (number != std::trunc(number))
            {
                return false;
            }
            if (type == Gfx::ShaderParamType::Int)
            {
                return number >= static_cast<double>(std::numeric_limits<int32>::min()) &&
                       number <= static_cast<double>(std::numeric_limits<int32>::max());
            }
            return number >= 0.0 && number <= static_cast<double>(std::numeric_limits<uint32>::max());
        }

        // HLSL の bool・int・uint・float は、どれも 1 成分 32 ビットで並ぶ
        [[nodiscard]] uint32 NumberBits(Gfx::ShaderParamType type, double number) noexcept
        {
            if (IsFloatType(type))
            {
                return std::bit_cast<uint32>(static_cast<float>(number));
            }
            if (type == Gfx::ShaderParamType::Int)
            {
                return std::bit_cast<uint32>(static_cast<int32>(number));
            }
            return static_cast<uint32>(number);
        }

        // 頂点と画素で並びが食い違うシェーダーの組は、材質の値なしで描く
        [[nodiscard]] Gfx::ShaderParamLayout MakeParamLayout(const std::string& path,
                                                             const Shader* vertexShader,
                                                             const Shader* pixelShader)
        {
            if (vertexShader->D3D() == nullptr || pixelShader->D3D() == nullptr)
            {
                return {};
            }
            std::optional<Gfx::ShaderParamLayout> merged =
                Gfx::MergeMaterialParams(vertexShader->D3D()->MaterialParams(), pixelShader->D3D()->MaterialParams());
            if (!merged)
            {
                NS_LOG_WARN(Graphics,
                            "マテリアル {} の頂点と画素のシェーダーで MaterialParams の並びが違うので、材質の値なしで描く",
                            path);
                return {};
            }
            return std::move(*merged);
        }

        struct D3DMaterialSources
        {
            const Shader* vertexShader = nullptr;
            const Shader* pixelShader = nullptr;
            Gfx::BlendMode blend = Gfx::BlendMode::Opaque;
            const Gfx::ShaderParamLayout* layout = nullptr;
            const MaterialParamValues* params = nullptr;
            std::span<Texture* const> textures;
        };

        [[nodiscard]] std::unique_ptr<Gfx::D3DMaterial> MakeD3DMaterial(const D3DMaterialSources& sources)
        {
            NS_ASSERT(Graphics,
                      sources.vertexShader != nullptr && sources.pixelShader != nullptr,
                      "マテリアルにシェーダーが無い");

            // 定数バッファは Model が流す FrameCB に合わせる
            Gfx::D3DMaterialDesc desc{};
            desc.vertexShader = sources.vertexShader->D3D();
            desc.pixelShader = sources.pixelShader->D3D();
            desc.constantBufferSize = sizeof(Gfx::FrameCB);
            desc.materialParamsSize = sources.layout->bufferSize;
            desc.blend = sources.blend;
            std::unique_ptr<Gfx::D3DMaterial> material = Gfx::D3DMaterial::Create(desc);
            if (sources.layout->bufferSize > 0u)
            {
                material->SetMaterialParams(PackMaterialParams(*sources.layout, *sources.params));
            }

            for (std::size_t i = 0; i < sources.textures.size(); ++i)
            {
                material->SetTexture(static_cast<uint>(i), sources.textures[i]->D3D());
            }
            return material;
        }
    } // namespace

    bool FitsParam(const Gfx::ShaderParam& param, const MaterialParamValue& value) noexcept
    {
        const std::size_t count = ComponentCount(param.type);
        if (count == 0 || value.numbers.size() != count)
        {
            return false;
        }
        if (param.type == Gfx::ShaderParamType::Bool)
        {
            return value.isBool && (value.numbers[0] == 0.0 || value.numbers[0] == 1.0);
        }
        if (value.isBool)
        {
            return false;
        }
        for (const double number : value.numbers)
        {
            if (!FitsNumber(param.type, number))
            {
                return false;
            }
        }
        return true;
    }

    MaterialParamValue InitialParamValue(const Gfx::ShaderParam& param)
    {
        MaterialParamValue value;
        const std::size_t count = ComponentCount(param.type);
        for (std::size_t i = 0; i < count; ++i)
        {
            const uint32 bits = param.initialBits[i];
            if (IsFloatType(param.type))
            {
                value.numbers.push_back(static_cast<double>(std::bit_cast<float>(bits)));
            }
            else if (param.type == Gfx::ShaderParamType::Int)
            {
                value.numbers.push_back(static_cast<double>(std::bit_cast<int32>(bits)));
            }
            else if (param.type == Gfx::ShaderParamType::Bool)
            {
                value.isBool = true;
                if (bits != 0u)
                {
                    value.numbers.push_back(1.0);
                }
                else
                {
                    value.numbers.push_back(0.0);
                }
            }
            else
            {
                value.numbers.push_back(static_cast<double>(bits));
            }
        }
        return value;
    }

    std::vector<std::byte> PackMaterialParams(const Gfx::ShaderParamLayout& layout, const MaterialParamValues& values)
    {
        std::vector<std::byte> bytes(layout.bufferSize, std::byte{0});
        for (const Gfx::ShaderParam& param : layout.params)
        {
            const std::size_t count = ComponentCount(param.type);
            NS_ASSERT(Graphics,
                      param.offset + count * sizeof(uint32) <= bytes.size(),
                      "材質の値 {} がバッファの外にはみ出す",
                      param.name);
            if (count == 0 || param.offset + count * sizeof(uint32) > bytes.size())
            {
                continue;
            }

            std::array<uint32, 4> bits = param.initialBits;
            const MaterialParamValues::const_iterator found = values.find(param.name);
            if (found != values.end() && FitsParam(param, found->second))
            {
                for (std::size_t i = 0; i < count; ++i)
                {
                    bits[i] = NumberBits(param.type, found->second.numbers[i]);
                }
            }
            std::memcpy(bytes.data() + param.offset, bits.data(), count * sizeof(uint32));
        }
        return bytes;
    }

    Material::Material(std::string path,
                       MaterialFileDesc file,
                       Shader* vertexShader,
                       Shader* pixelShader,
                       std::span<Texture* const> textures)
        : m_path(std::move(path)), m_file(std::move(file)), m_vertexShader(vertexShader), m_pixelShader(pixelShader)
    {
        m_paramLayout = MakeParamLayout(m_path, m_vertexShader, m_pixelShader);
        m_material = MakeD3DMaterial(D3DMaterialSources{.vertexShader = m_vertexShader,
                                                        .pixelShader = m_pixelShader,
                                                        .blend = m_file.blend,
                                                        .layout = &m_paramLayout,
                                                        .params = &m_file.params,
                                                        .textures = textures});
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

    bool Material::Rebuild(Shader* vertexShader, Shader* pixelShader, std::span<Texture* const> textures)
    {
        NS_ASSERT(Graphics, vertexShader != nullptr && pixelShader != nullptr, "マテリアルにシェーダーが無い");
        Gfx::ShaderParamLayout layout = MakeParamLayout(m_path, vertexShader, pixelShader);
        std::unique_ptr<Gfx::D3DMaterial> material = MakeD3DMaterial(D3DMaterialSources{.vertexShader = vertexShader,
                                                                                         .pixelShader = pixelShader,
                                                                                         .blend = m_file.blend,
                                                                                         .layout = &layout,
                                                                                         .params = &m_file.params,
                                                                                         .textures = textures});
        if (!material->IsValid())
        {
            NS_LOG_WARN(Graphics, "マテリアル {} を作り直せないので、前の D3DMaterial で描き続ける", m_path);
            return false;
        }
        m_vertexShader = vertexShader;
        m_pixelShader = pixelShader;
        m_paramLayout = std::move(layout);
        m_material = std::move(material);
        return true;
    }

    const Gfx::ShaderParamLayout& Material::ParamLayout() const noexcept
    {
        return m_paramLayout;
    }

    MaterialParamValue Material::ParamValue(const Gfx::ShaderParam& param) const
    {
        const MaterialParamValues::const_iterator found = m_file.params.find(param.name);
        if (found != m_file.params.end() && FitsParam(param, found->second))
        {
            return found->second;
        }
        return InitialParamValue(param);
    }

    bool Material::SetParamValue(const Gfx::ShaderParam& param, const MaterialParamValue& value)
    {
        if (!FitsParam(param, value))
        {
            NS_LOG_WARN(Graphics, "マテリアル {} の値 {} は項目の型に合わないので受けない", m_path, param.name);
            return false;
        }
        m_file.params[param.name] = value;
        if (m_material != nullptr && m_paramLayout.bufferSize > 0u)
        {
            m_material->SetMaterialParams(PackMaterialParams(m_paramLayout, m_file.params));
        }
        return true;
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
