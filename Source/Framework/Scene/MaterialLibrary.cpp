#include "Framework/Scene/MaterialLibrary.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Graphics/Shader.h"
#include "Framework/Graphics/Texture.h"
#include "Framework/Scene/Components/MeshRendererComponent.h"

#include <string>
#include <utility>

// json.hpp は /W4 で警告が出るため、 この TU でだけ警告を抑止して取り込む
#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

namespace NS::Scene
{
    namespace
    {
        [[nodiscard]] NS::Graphics::BlendMode ParseBlend(const std::string& value) noexcept
        {
            if (value == "Alpha")
                return NS::Graphics::BlendMode::Alpha;
            if (value == "Additive")
                return NS::Graphics::BlendMode::Additive;
            return NS::Graphics::BlendMode::Opaque;
        }
    } // namespace

    bool ParseMaterialJson(std::string_view jsonText, MaterialFileDesc& out, std::string& outError)
    {
        // 例外を投げない parse。 不正 JSON は is_discarded() で検知する
        const nlohmann::json j = nlohmann::json::parse(jsonText, nullptr, false);
        if (j.is_discarded())
        {
            outError = "JSON parse に失敗";
            return false;
        }
        if (!j.is_object())
        {
            outError = "ルートが object でない";
            return false;
        }

        // vs / ps は必須。 これが無いと Material を作れない
        if (!j.contains("vs") || !j["vs"].is_string() || !j.contains("ps") || !j["ps"].is_string())
        {
            outError = "vs / ps (string) が必要";
            return false;
        }
        out.vertexShader = j["vs"].get<std::string>();
        out.pixelShader = j["ps"].get<std::string>();

        out.textures.clear();
        if (j.contains("textures") && j["textures"].is_array())
        {
            for (const auto& tex : j["textures"])
            {
                if (tex.is_string())
                    out.textures.emplace_back(tex.get<std::string>());
            }
        }

        out.baseColor = NS::Math::Vector3{1.0f, 1.0f, 1.0f};
        if (j.contains("baseColor") && j["baseColor"].is_array() && j["baseColor"].size() == 3)
        {
            const auto& c = j["baseColor"];
            if (c[0].is_number() && c[1].is_number() && c[2].is_number())
                out.baseColor = NS::Math::Vector3{c[0].get<float>(), c[1].get<float>(), c[2].get<float>()};
        }

        out.blend = NS::Graphics::BlendMode::Opaque;
        if (j.contains("blend") && j["blend"].is_string())
            out.blend = ParseBlend(j["blend"].get<std::string>());

        outError.clear();
        return true;
    }

    MaterialLibrary::MaterialLibrary(std::filesystem::path baseDir) noexcept : m_baseDir(std::move(baseDir)) {}
    MaterialLibrary::~MaterialLibrary() = default;

    void MaterialLibrary::Clear() noexcept
    {
        // material が shader / texture を参照するので先に material から解放する
        m_materials.clear();
        m_textures.clear();
        m_shaders.clear();
    }

    NS::Graphics::Shader* MaterialLibrary::GetOrLoadShader(const std::filesystem::path& path)
    {
        // 表記揺れ (区切り文字 / . / ..) で同一ファイルが別キー扱いにならないよう正規化してから dedupe する
        const std::filesystem::path key = path.lexically_normal();
        if (const auto it = m_shaders.find(key); it != m_shaders.end())
            return it->second.get();
        auto shader = NS::Graphics::Shader::Create(key);
        NS::Graphics::Shader* raw = shader.get();
        m_shaders.emplace(key, std::move(shader));
        return raw;
    }

    NS::Graphics::Texture* MaterialLibrary::GetOrLoadTexture(const std::filesystem::path& path)
    {
        const std::filesystem::path key = path.lexically_normal();
        if (const auto it = m_textures.find(key); it != m_textures.end())
            return it->second.get();
        NS::Graphics::TextureDesc desc{};
        desc.path = key;
        desc.generateMipmaps = true;
        desc.sRGB = false;
        auto texture = NS::Graphics::Texture::Create(desc);
        NS::Graphics::Texture* raw = texture.get();
        m_textures.emplace(key, std::move(texture));
        return raw;
    }

    LoadedMaterial MaterialLibrary::Load(const std::filesystem::path& matPath)
    {
        const std::filesystem::path matKey = matPath.lexically_normal();
        if (const auto it = m_materials.find(matKey); it != m_materials.end())
            return LoadedMaterial{it->second.material.get(), it->second.baseColor};

        const auto textOpt = NS::Core::FileSystem::ReadAllText(matKey);
        if (!textOpt)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "MaterialLibrary: .mat 読込失敗: {}", matKey.string());
            return LoadedMaterial{};
        }

        MaterialFileDesc fileDesc{};
        std::string parseError;
        if (!ParseMaterialJson(*textOpt, fileDesc, parseError))
        {
            NS_LOG_ERROR(
                ::NS::Core::LogCat::Graphics, "MaterialLibrary: .mat 解析失敗 ({}): {}", parseError, matKey.string());
            return LoadedMaterial{};
        }

        // 相対パスは構築時の baseDir (通常は exe ディレクトリ) 基準で解決する
        const auto resolve = [this](const std::filesystem::path& p) { return p.is_absolute() ? p : (m_baseDir / p); };

        NS::Graphics::Shader* vertexShader = GetOrLoadShader(resolve(fileDesc.vertexShader));
        NS::Graphics::Shader* pixelShader = GetOrLoadShader(resolve(fileDesc.pixelShader));

        // CB は MeshRendererComponent::Draw が流す FrameCB に合わせる (slot 0)
        NS::Graphics::MaterialDesc matDesc{};
        matDesc.vertexShader = vertexShader;
        matDesc.pixelShader = pixelShader;
        matDesc.constantBufferSize = sizeof(FrameCB);
        matDesc.cbSlot = 0;
        matDesc.blend = fileDesc.blend;
        auto material = NS::Graphics::Material::Create(matDesc);

        for (std::size_t i = 0; i < fileDesc.textures.size(); ++i)
        {
            NS::Graphics::Texture* texture = GetOrLoadTexture(resolve(fileDesc.textures[i]));
            material->SetTexture(static_cast<unsigned>(i), texture);
        }

        MaterialRecord record{};
        record.material = std::move(material);
        record.baseColor = fileDesc.baseColor;
        NS::Graphics::Material* rawMaterial = record.material.get();
        const NS::Math::Vector3 color = record.baseColor;
        m_materials.emplace(matKey, std::move(record));
        return LoadedMaterial{rawMaterial, color};
    }
} // namespace NS::Scene
