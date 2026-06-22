#include "Framework/Scene/AssetManager.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Graphics/GltfLoader.h"
#include "Framework/Graphics/Material.h"
#include "Framework/Graphics/Mesh.h"
#include "Framework/Graphics/MeshPrimitives.h"
#include "Framework/Graphics/Shader.h"
#include "Framework/Graphics/SkeletalMesh.h"
#include "Framework/Graphics/StaticMesh.h"
#include "Framework/Graphics/Texture.h"
#include "Framework/Graphics/TextureArray.h"
#include "Framework/Math/Math.h"
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
        constexpr const char* kBuiltinCube = "cube";
        constexpr const char* kBuiltinWedge45 = "wedge45";
        constexpr const char* kBuiltinWedge30 = "wedge30";
        constexpr const char* kBuiltinWedge22 = "wedge22";
        constexpr const char* kBuiltinWedge15 = "wedge15";
        constexpr const char* kBuiltinPole = "pole";
        constexpr const char* kBuiltinShadowQuad = "shadowQuad";

        constexpr const char* kSharedPlayer = "player";
        constexpr const char* kSharedBlock = "block";
        constexpr const char* kSharedWater = "water";
        constexpr const char* kSharedShadow = "shadow";

        // MeshGeometry を MeshDesc へ詰めて StaticMesh を生成する。 geom はこの呼出中のみ参照される
        [[nodiscard]] std::unique_ptr<NS::Graphics::StaticMesh> MakeStaticMesh(const NS::Graphics::MeshGeometry& geom)
        {
            NS::Graphics::MeshDesc desc{};
            desc.vertices = geom.vertices.data();
            desc.vertexCount = geom.vertices.size();
            desc.indices = geom.indices.data();
            desc.indexCount = geom.indices.size();
            return NS::Graphics::StaticMesh::Create(desc);
        }

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

    AssetManager::AssetManager(std::filesystem::path baseDir) noexcept : m_baseDir(std::move(baseDir)) {}
    AssetManager::~AssetManager() = default;

    NS::Graphics::Shader* AssetManager::GetOrLoadShader(const std::filesystem::path& path)
    {
        // 表記揺れ (区切り文字 / . / ..) で同一ファイルが別キー扱いにならないよう正規化してから dedupe する
        const std::filesystem::path key = path.lexically_normal();
        if (const auto it = m_shaders.find(key); it != m_shaders.end())
            return it->second.get();
        auto shader = NS::Graphics::Shader::Create(key);
        NS::Graphics::Shader* raw = shader.get();
        if (raw->IsUsingFallback())
            NS_LOG_WARN(::NS::Core::LogCat::Graphics,
                        "AssetManager: shader の読込/コンパイル失敗、 fallback 描画: {}",
                        key.string());
        m_shaders.emplace(key, std::move(shader));
        return raw;
    }

    NS::Graphics::Texture* AssetManager::GetOrLoadTexture(const std::filesystem::path& path)
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

    NS::Graphics::Mesh* AssetManager::GetOrLoadMesh(const std::filesystem::path& path)
    {
        const std::filesystem::path key = path.lexically_normal();
        if (const auto it = m_meshes.find(key); it != m_meshes.end())
            return it->second.get();
        // file mesh (.glb 等) のロードは消費者が出た時に実装する。 dedupe の枠だけ用意し、 現状は未対応
        NS_LOG_WARN(
            ::NS::Core::LogCat::Graphics, "AssetManager::GetOrLoadMesh: file mesh ロード未対応: {}", key.string());
        return nullptr;
    }

    LoadedSkinnedModel AssetManager::GetOrLoadSkinnedModel(const std::filesystem::path& path)
    {
        const std::filesystem::path key = path.lexically_normal();
        auto it = m_skinnedModels.find(key);
        if (it == m_skinnedModels.end())
        {
            NS::Graphics::SkinnedMeshData data = NS::Graphics::LoadGltfSkinnedMesh(key.string());
            if (!data.IsValid())
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "AssetManager: skinned glTF 読込失敗: {}", key.string());
                return LoadedSkinnedModel{};
            }

            NS::Graphics::SkinnedMeshDesc smd{};
            smd.vertices = data.vertices.data();
            smd.vertexCount = data.vertices.size();
            smd.indices = data.indices.data();
            smd.indexCount = data.indices.size();
            smd.boneCount = data.skeleton.BoneCount();

            SkinnedModelRecord record{};
            record.mesh = NS::Graphics::SkeletalMesh::Create(smd);
            if (record.mesh == nullptr || !record.mesh->IsValid())
            {
                // GPU buffer 生成に失敗。 ダッド mesh をキャッシュせず無効を返す (Draw が無音 no-op になるのを防ぐ)
                NS_LOG_ERROR(
                    ::NS::Core::LogCat::Graphics, "AssetManager: skinned mesh の GPU 生成失敗: {}", key.string());
                return LoadedSkinnedModel{};
            }

            // bind ポーズ頂点の境界を求めて配置スケール計算用に持たせる
            record.boundsMin = data.vertices.front().position;
            record.boundsMax = record.boundsMin;
            for (const NS::Graphics::SkinnedVertex& v : data.vertices)
            {
                record.boundsMin = NS::Math::Vector3::Min(record.boundsMin, v.position);
                record.boundsMax = NS::Math::Vector3::Max(record.boundsMax, v.position);
            }
            record.skeleton = std::move(data.skeleton);
            record.clips = std::move(data.animations);
            it = m_skinnedModels.emplace(key, std::move(record)).first;
        }

        LoadedSkinnedModel out{};
        out.mesh = it->second.mesh.get();
        out.skeleton = it->second.skeleton;
        out.clips = it->second.clips;
        out.boundsMin = it->second.boundsMin;
        out.boundsMax = it->second.boundsMax;
        out.valid = true;
        return out;
    }

    void AssetManager::RegisterBuiltins()
    {
        const NS::Math::Vector3 half{0.5f, 0.5f, 0.5f};
        m_builtins.emplace(kBuiltinCube, MakeStaticMesh(NS::Graphics::MakeCube(half)));
        m_builtins.emplace(kBuiltinWedge45, MakeStaticMesh(NS::Graphics::MakeWedge(45.0f, half)));
        m_builtins.emplace(kBuiltinWedge30, MakeStaticMesh(NS::Graphics::MakeWedge(30.0f, half)));
        m_builtins.emplace(kBuiltinWedge22, MakeStaticMesh(NS::Graphics::MakeWedge(22.5f, half)));
        m_builtins.emplace(kBuiltinWedge15, MakeStaticMesh(NS::Graphics::MakeWedge(15.0f, half)));
        m_builtins.emplace(kBuiltinPole, MakeStaticMesh(NS::Graphics::MakeCylinder(0.15f, 1.0f, 12)));
        m_builtins.emplace(kBuiltinShadowQuad, MakeStaticMesh(NS::Graphics::MakePlane(NS::Math::Vector2{0.5f, 0.5f})));
    }

    NS::Graphics::StaticMesh* AssetManager::Builtin(std::string_view name) const noexcept
    {
        const auto it = m_builtins.find(std::string(name));
        return it != m_builtins.end() ? it->second.get() : nullptr;
    }

    LoadedMaterial AssetManager::LoadMaterial(const std::filesystem::path& matPath)
    {
        const std::filesystem::path matKey = matPath.lexically_normal();
        if (const auto it = m_materials.find(matKey); it != m_materials.end())
            return LoadedMaterial{it->second.material.get(), it->second.baseColor};

        const auto textOpt = NS::Core::FileSystem::ReadAllText(matKey);
        if (!textOpt)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "AssetManager: .mat 読込失敗: {}", matKey.string());
            return LoadedMaterial{};
        }

        MaterialFileDesc fileDesc{};
        std::string parseError;
        if (!ParseMaterialJson(*textOpt, fileDesc, parseError))
        {
            NS_LOG_ERROR(
                ::NS::Core::LogCat::Graphics, "AssetManager: .mat 解析失敗 ({}): {}", parseError, matKey.string());
            return LoadedMaterial{};
        }

        // 相対 path は構築時の baseDir 基準で解決する
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

    void AssetManager::RegisterSharedMaterials()
    {
        const auto shaderPath = [this](const char* name) { return m_baseDir / "Shaders" / name; };
        NS::Graphics::Shader* standardVS = GetOrLoadShader(shaderPath("standard.vs.hlsl"));
        NS::Graphics::Shader* playerPS = GetOrLoadShader(shaderPath("player.ps.hlsl"));
        NS::Graphics::Texture* baseTexture = GetOrLoadTexture(m_baseDir / "Assets" / "Textures" / "cube_test.png");

        // CB は MeshRendererComponent::Draw が流す FrameCB に合わせる (slot 0)
        NS::Graphics::MaterialDesc base{};
        base.vertexShader = standardVS;
        base.pixelShader = playerPS;
        base.constantBufferSize = sizeof(FrameCB);
        base.cbSlot = 0;

        // player: 単一 Texture2D。 slot0 に基準テクスチャを bind する
        {
            auto mat = NS::Graphics::Material::Create(base);
            mat->SetTexture(0, baseTexture);
            m_sharedMaterials.emplace(kSharedPlayer, std::move(mat));
        }
        // block: player と同じ VS/PS/CB を共有する CB 搬入路。 実 VS/PS/Texture は InstanceBatcher が上書きする
        // slot0 は外側で TextureArray を bind するため SetTexture 禁止 — 呼ぶと Material::Bind が SRV を上書きする
        {
            auto mat = NS::Graphics::Material::Create(base);
            m_sharedMaterials.emplace(kSharedBlock, std::move(mat));
        }
        // water: alpha<1 を出す water.ps + Alpha ブレンドの専用 material
        {
            NS::Graphics::MaterialDesc desc = base;
            desc.pixelShader = GetOrLoadShader(shaderPath("water.ps.hlsl"));
            desc.blend = NS::Graphics::BlendMode::Alpha;
            auto mat = NS::Graphics::Material::Create(desc);
            mat->SetTexture(0, baseTexture);
            m_sharedMaterials.emplace(kSharedWater, std::move(mat));
        }
        // shadow: shadow.ps が放射状アルファを生成するためテクスチャ不要、 Alpha ブレンド
        {
            NS::Graphics::MaterialDesc desc = base;
            desc.pixelShader = GetOrLoadShader(shaderPath("shadow.ps.hlsl"));
            desc.blend = NS::Graphics::BlendMode::Alpha;
            m_sharedMaterials.emplace(kSharedShadow, NS::Graphics::Material::Create(desc));
        }
    }

    NS::Graphics::Material* AssetManager::SharedMaterial(std::string_view name) const noexcept
    {
        const auto it = m_sharedMaterials.find(std::string(name));
        return it != m_sharedMaterials.end() ? it->second.get() : nullptr;
    }

    NS::Graphics::TextureArray* AssetManager::GetOrCreateTextureArray(std::string_view name,
                                                                      const NS::Graphics::TextureArrayDesc& desc)
    {
        const std::string key(name);
        if (const auto it = m_textureArrays.find(key); it != m_textureArrays.end())
            return it->second.get();
        auto array = NS::Graphics::TextureArray::Create(desc);
        NS::Graphics::TextureArray* raw = array.get();
        m_textureArrays.emplace(key, std::move(array));
        return raw;
    }

    NS::Graphics::TextureArray* AssetManager::TextureArrayByName(std::string_view name) const noexcept
    {
        const auto it = m_textureArrays.find(std::string(name));
        return it != m_textureArrays.end() ? it->second.get() : nullptr;
    }

    bool AssetManager::Reload(const std::filesystem::path& path)
    {
        const std::filesystem::path key = path.lexically_normal();
        if (const auto it = m_shaders.find(key); it != m_shaders.end())
            return it->second->Reload();
        NS_LOG_WARN(::NS::Core::LogCat::Graphics, "AssetManager::Reload: 未キャッシュの path: {}", key.string());
        return false;
    }

    std::size_t AssetManager::ReloadAllShaders()
    {
        std::size_t reloaded = 0;
        for (auto& entry : m_shaders)
        {
            if (entry.second->Reload())
                ++reloaded;
        }
        NS_LOG_INFO(
            ::NS::Core::LogCat::Graphics, "AssetManager: shader reload {} / {} 本成功", reloaded, m_shaders.size());
        return reloaded;
    }

    void AssetManager::Clear() noexcept
    {
        // material は leaf (shader / texture) を参照するので先に解放する
        m_sharedMaterials.clear();
        m_materials.clear();
        m_textureArrays.clear();
        m_textures.clear();
        m_shaders.clear();
        m_meshes.clear();
        m_skinnedModels.clear();
        m_builtins.clear();
    }
} // namespace NS::Scene
