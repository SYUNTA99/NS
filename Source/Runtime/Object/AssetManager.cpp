#include "Runtime/Object/AssetManager.h"

#include "Runtime/Core/Filesystem.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/FrameConstants.h"
#include "Runtime/Graphics/GltfLoader.h"
#include "Runtime/Graphics/Material.h"
#include "Runtime/Graphics/Mesh.h"
#include "Runtime/Graphics/MeshPrimitives.h"
#include "Runtime/Graphics/Retarget.h"
#include "Runtime/Graphics/Shader.h"
#include "Runtime/Graphics/SkeletalMesh.h"
#include "Runtime/Graphics/StaticMesh.h"
#include "Runtime/Graphics/Texture.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/MeshRendererComponent.h"

#include <memory>

// json.hpp は /W4 で警告が出るため、 この翻訳単位でだけ警告を抑止して取り込む
#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

namespace NS::Object
{
    std::optional<std::filesystem::path> ResolveContentPath(const std::string& relative)
    {
        namespace fs = std::filesystem;
        const fs::path root = NS::Core::FileSystem::ContentRoot().lexically_normal();
        fs::path resolved = (root / relative).lexically_normal();
        const fs::path rel = resolved.lexically_relative(root);

        // ディレクトリトラバーサルの防止
        if (rel.empty() || *rel.begin() == fs::path{".."})
        {
            return std::nullopt;
        }
        return resolved;
    }

    NS::Graphics::Mesh* ResolveMeshFromRef(AssetManager& assets, const std::string& meshRef)
    {
        if (meshRef.empty())
            return nullptr;

        if (NS::Graphics::StaticMesh* builtin = assets.Builtin(meshRef))
        {
            return builtin;
        }

        const std::optional<std::filesystem::path> resolved = ResolveContentPath(meshRef);
        if (!resolved)
            return nullptr;
        return assets.GetOrLoadMesh(*resolved);
    }

    namespace
    {
        constexpr const char* k_BuiltinCube = "cube";
        constexpr const char* k_BuiltinSphere = "sphere";
        constexpr const char* k_BuiltinWedge45 = "wedge45";
        constexpr const char* k_BuiltinWedge30 = "wedge30";
        constexpr const char* k_BuiltinWedge22 = "wedge22";
        constexpr const char* k_BuiltinWedge15 = "wedge15";
        constexpr const char* k_BuiltinShadowQuad = "shadowQuad";

        constexpr const char* k_SharedPlayer = "player";
        constexpr const char* k_SharedWater = "water";
        constexpr const char* k_SharedShadow = "shadow";

        // geom はこの呼出中のみ参照される
        [[nodiscard]] std::unique_ptr<NS::Graphics::StaticMesh> MakeStaticMesh(const NS::Graphics::MeshGeometry& geom)
        {
            NS::Graphics::MeshDesc desc{};
            desc.vertices = geom.vertices.data();
            desc.vertexCount = geom.vertices.size();
            desc.indices = geom.indices.data();
            desc.indexCount = geom.indices.size();
            if (geom.hasBounds)
                desc.precomputedBounds = &geom.bounds;
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

        if (!j.contains("vs") || !j["vs"].is_string() || !j.contains("ps") || !j["ps"].is_string())
        {
            outError = "vs / ps (string) が必要";
            return false;
        }
        out.vertexShader = j["vs"].get<std::string>();
        out.pixelShader = j["ps"].get<std::string>();

        // textures は任意
        out.textures.clear();
        if (j.contains("textures") && j["textures"].is_array())
        {
            for (const auto& tex : j["textures"])
            {
                if (tex.is_string())
                    out.textures.emplace_back(tex.get<std::string>());
            }
        }

        // baseColor は任意の 3 要素
        out.baseColor = NS::Core::Vector3{1.0f, 1.0f, 1.0f};
        if (j.contains("baseColor") && j["baseColor"].is_array() && j["baseColor"].size() == 3)
        {
            const auto& c = j["baseColor"];
            if (c[0].is_number() && c[1].is_number() && c[2].is_number())
                out.baseColor = NS::Core::Vector3{c[0].get<float>(), c[1].get<float>(), c[2].get<float>()};
        }

        // blend は任意
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
        // 区切り文字や . / .. の表記揺れで同一ファイルが別キー扱いにならないよう正規化してから重複をまとめる
        const std::filesystem::path key = path.lexically_normal();
        if (const auto it = m_shaders.find(key); it != m_shaders.end())
            return it->second.get();
        auto shader = NS::Graphics::Shader::Create(key);
        NS::Graphics::Shader* raw = shader.get();
        if (raw->IsUsingFallback())
            NS_LOG_WARN(Graphics, "AssetManager: shader の読込/コンパイル失敗、 fallback 描画: {}", key.string());
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
        // null エントリは負キャッシュした失敗 path を表す。 get() が nullptr を返し再読込を短絡する
        if (const auto it = m_meshes.find(key); it != m_meshes.end())
            return it->second.get();

        const NS::Graphics::MeshGeometry geom = NS::Graphics::LoadGltfMesh(key.string());
        if (geom.vertices.empty() || geom.indices.empty())
        {
            NS_LOG_WARN(Graphics, "AssetManager::GetOrLoadMesh: mesh の読込失敗 / 空: {}", key.string());
            // 壊れた path を負キャッシュし、 同じ参照を持つ object 群が毎回ディスク I/O を踏むのを防ぐ
            m_meshes.emplace(key, nullptr);
            return nullptr;
        }

        std::unique_ptr<NS::Graphics::StaticMesh> mesh = MakeStaticMesh(geom);
        if (mesh == nullptr || !mesh->IsValid())
        {
            NS_LOG_ERROR(Graphics, "AssetManager::GetOrLoadMesh: mesh の GPU 生成失敗: {}", key.string());
            // GPU 生成失敗も負キャッシュする。 修正後の再試行は Clear() で解いてから
            m_meshes.emplace(key, nullptr);
            return nullptr;
        }

        NS::Graphics::Mesh* raw = mesh.get();
        m_meshes.emplace(key, std::move(mesh));
        return raw;
    }

    std::size_t AssetManager::MeshCacheSize() const noexcept
    {
        return m_meshes.size();
    }

    LoadedSkinnedModel AssetManager::GetOrLoadSkinnedModel(const std::filesystem::path& path)
    {
        const std::filesystem::path key = path.lexically_normal();
        auto it = m_skinnedModels.find(key);
        if (it == m_skinnedModels.end())
        {
            // glTF から skinned mesh を読む
            NS::Graphics::SkinnedMeshData data = NS::Graphics::LoadGltfSkinnedMesh(key.string());
            if (!data.IsValid())
            {
                NS_LOG_ERROR(Graphics, "AssetManager: skinned glTF 読込失敗: {}", key.string());
                return LoadedSkinnedModel{};
            }

            if (data.vertices.empty())
            {
                NS_LOG_ERROR(Graphics, "AssetManager: skinned mesh に頂点が無い: {}", key.string());
                return LoadedSkinnedModel{};
            }

            // GPU 生成用の記述子を組む
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
                // GPU buffer 生成に失敗。 壊れた mesh をキャッシュせず無効を返し、 Draw が無音で何もしないのを防ぐ
                NS_LOG_ERROR(Graphics, "AssetManager: skinned mesh の GPU 生成失敗: {}", key.string());
                return LoadedSkinnedModel{};
            }

            // bind ポーズ頂点の境界を求めて配置スケール計算用に持たせる
            record.boundsMin = data.vertices.front().position;
            record.boundsMax = record.boundsMin;
            for (const NS::Graphics::SkinnedVertex& v : data.vertices)
            {
                record.boundsMin = NS::Core::Vector3::Min(record.boundsMin, v.position);
                record.boundsMax = NS::Core::Vector3::Max(record.boundsMax, v.position);
            }
            record.skeleton = std::move(data.skeleton);
            record.clips = std::move(data.animations);
            it = m_skinnedModels.emplace(key, std::move(record)).first;
        }

        // record への参照を渡す。 record は挿入後に書き換えないので、 参照は Clear() まで有効
        LoadedSkinnedModel out{};
        out.mesh = it->second.mesh.get();
        out.skeleton = &it->second.skeleton;
        out.clips = &it->second.clips;
        out.boundsMin = it->second.boundsMin;
        out.boundsMax = it->second.boundsMax;
        out.valid = true;
        return out;
    }

    const NS::Graphics::AnimationSource* AssetManager::GetOrLoadAnimationSource(const std::filesystem::path& path)
    {
        const std::filesystem::path key = path.lexically_normal();
        // null エントリは負キャッシュした失敗 path を表す。 get() が nullptr を返し再読込を短絡する
        if (const auto it = m_animationSources.find(key); it != m_animationSources.end())
            return it->second.get();

        NS::Graphics::AnimationSource source = NS::Graphics::LoadGltfAnimationSource(key.string());
        if (!source.IsValid())
        {
            NS_LOG_WARN(Graphics, "AssetManager: アニメーション glTF の読込失敗 / 空: {}", key.string());
            // 壊れた path を負キャッシュし、 毎回のディスク I/O を防ぐ。 再試行は Clear() から
            m_animationSources.emplace(key, nullptr);
            return nullptr;
        }

        auto owned = std::make_unique<NS::Graphics::AnimationSource>(std::move(source));
        const NS::Graphics::AnimationSource* raw = owned.get();
        m_animationSources.emplace(key, std::move(owned));
        return raw;
    }

    const std::vector<NS::Graphics::AnimationClip>* AssetManager::GetOrLoadBoundClips(
        const std::filesystem::path& clipPath, const std::filesystem::path& modelPath)
    {
        const std::pair<std::filesystem::path, std::filesystem::path> key{clipPath.lexically_normal(),
                                                                          modelPath.lexically_normal()};
        if (const auto it = m_boundClips.find(key); it != m_boundClips.end())
            return it->second.get();

        const NS::Graphics::AnimationSource* source = GetOrLoadAnimationSource(key.first);
        if (source == nullptr)
        {
            // クリップ側の読込失敗は上流で負キャッシュ済み。 組としても負キャッシュする
            m_boundClips.emplace(key, nullptr);
            return nullptr;
        }

        const LoadedSkinnedModel model = GetOrLoadSkinnedModel(key.second);
        if (!model.valid || model.skeleton == nullptr)
        {
            // model 側の失敗は GetOrLoadSkinnedModel がキャッシュせず再試行できるようにしている。 組で恒久化しない
            return nullptr;
        }

        // 結合で index を振り直した複製は避けられない派生データだが、 所有はこちら側なので
        // 同じ組で解決する全インスタンスがこの 1 本を共有する
        auto bound = std::make_unique<std::vector<NS::Graphics::AnimationClip>>(
            NS::Graphics::BindClipsByName(source->animations, source->skeleton, *model.skeleton));
        const std::vector<NS::Graphics::AnimationClip>* raw = bound.get();
        m_boundClips.emplace(key, std::move(bound));
        return raw;
    }

    void AssetManager::RegisterBuiltins()
    {
        const NS::Core::Vector3 half{0.5f, 0.5f, 0.5f};
        m_builtins.emplace(k_BuiltinCube, MakeStaticMesh(NS::Graphics::MakeCube(half)));
        m_builtins.emplace(k_BuiltinSphere, MakeStaticMesh(NS::Graphics::MakeSphere(half.x)));
        m_builtins.emplace(k_BuiltinWedge45, MakeStaticMesh(NS::Graphics::MakeSlope(45.0f, half)));
        m_builtins.emplace(k_BuiltinWedge30, MakeStaticMesh(NS::Graphics::MakeSlope(30.0f, half)));
        m_builtins.emplace(k_BuiltinWedge22, MakeStaticMesh(NS::Graphics::MakeSlope(22.5f, half)));
        m_builtins.emplace(k_BuiltinWedge15, MakeStaticMesh(NS::Graphics::MakeSlope(15.0f, half)));
        m_builtins.emplace(k_BuiltinShadowQuad, MakeStaticMesh(NS::Graphics::MakePlane(NS::Core::Vector2{0.5f, 0.5f})));
    }

    NS::Graphics::StaticMesh* AssetManager::Builtin(std::string_view name) const noexcept
    {
        const auto it = m_builtins.find(std::string(name));
        if (it != m_builtins.end())
            return it->second.get();
        return nullptr;
    }

    LoadedMaterial AssetManager::LoadMaterial(const std::filesystem::path& matPath)
    {
        const std::filesystem::path matKey = matPath.lexically_normal();
        if (const auto it = m_materials.find(matKey); it != m_materials.end())
            return LoadedMaterial{it->second.material.get(), it->second.baseColor};

        // .mat を読む
        const auto textOpt = NS::Core::FileSystem::ReadAllText(matKey);
        if (!textOpt)
        {
            NS_LOG_ERROR(Graphics, "AssetManager: .mat 読込失敗: {}", matKey.string());
            return LoadedMaterial{};
        }

        // JSON を MaterialFileDesc へ解析する
        MaterialFileDesc fileDesc{};
        std::string parseError;
        if (!ParseMaterialJson(*textOpt, fileDesc, parseError))
        {
            NS_LOG_ERROR(Graphics, "AssetManager: .mat 解析失敗 ({}): {}", parseError, matKey.string());
            return LoadedMaterial{};
        }

        // 相対 path は構築時の baseDir 基準で解決する
        const auto resolve = [this](const std::filesystem::path& p) -> std::filesystem::path {
            if (p.is_absolute())
                return p;
            return m_baseDir / p;
        };

        NS::Graphics::Shader* vertexShader = GetOrLoadShader(resolve(fileDesc.vertexShader));
        NS::Graphics::Shader* pixelShader = GetOrLoadShader(resolve(fileDesc.pixelShader));

        // CB は slot 0 で MeshRendererComponent が流す FrameCB に合わせる
        NS::Graphics::MaterialDesc matDesc{};
        matDesc.vertexShader = vertexShader;
        matDesc.pixelShader = pixelShader;
        matDesc.constantBufferSize = sizeof(NS::Graphics::FrameCB);
        matDesc.cbSlot = 0;
        matDesc.blend = fileDesc.blend;
        auto material = NS::Graphics::Material::Create(matDesc);

        // texture を slot 順に bind する
        for (std::size_t i = 0; i < fileDesc.textures.size(); ++i)
        {
            NS::Graphics::Texture* texture = GetOrLoadTexture(resolve(fileDesc.textures[i]));
            material->SetTexture(static_cast<unsigned>(i), texture);
        }

        MaterialRecord record{};
        record.material = std::move(material);
        record.baseColor = fileDesc.baseColor;
        NS::Graphics::Material* rawMaterial = record.material.get();
        const NS::Core::Vector3 color = record.baseColor;
        m_materials.emplace(matKey, std::move(record));
        return LoadedMaterial{rawMaterial, color};
    }

    void AssetManager::RegisterSharedMaterials()
    {
        const auto shaderPath = [this](const char* name) { return m_baseDir / "Shaders" / name; };
        NS::Graphics::Shader* standardVS = GetOrLoadShader(shaderPath("standard.vs.hlsl"));
        NS::Graphics::Shader* playerPS = GetOrLoadShader(shaderPath("player.ps.hlsl"));
        NS::Graphics::Texture* baseTexture = GetOrLoadTexture(m_baseDir / "Assets" / "Textures" / "cube_test.png");

        // CB は slot 0 で MeshRendererComponent が流す FrameCB に合わせる
        NS::Graphics::MaterialDesc base{};
        base.vertexShader = standardVS;
        base.pixelShader = playerPS;
        base.constantBufferSize = sizeof(NS::Graphics::FrameCB);
        base.cbSlot = 0;

        // player: 単一 Texture2D。 slot0 に基準テクスチャを bind する
        {
            auto mat = NS::Graphics::Material::Create(base);
            mat->SetTexture(0, baseTexture);
            m_sharedMaterials.emplace(k_SharedPlayer, std::move(mat));
        }
        // water: alpha<1 を出す water.ps + Alpha ブレンドの専用 material
        {
            NS::Graphics::MaterialDesc desc = base;
            desc.pixelShader = GetOrLoadShader(shaderPath("water.ps.hlsl"));
            desc.blend = NS::Graphics::BlendMode::Alpha;
            auto mat = NS::Graphics::Material::Create(desc);
            mat->SetTexture(0, baseTexture);
            m_sharedMaterials.emplace(k_SharedWater, std::move(mat));
        }
        // shadow: shadow.ps が放射状アルファを生成するためテクスチャ不要、 Alpha ブレンド
        {
            NS::Graphics::MaterialDesc desc = base;
            desc.pixelShader = GetOrLoadShader(shaderPath("shadow.ps.hlsl"));
            desc.blend = NS::Graphics::BlendMode::Alpha;
            m_sharedMaterials.emplace(k_SharedShadow, NS::Graphics::Material::Create(desc));
        }
    }

    NS::Graphics::Material* AssetManager::SharedMaterial(std::string_view name) const noexcept
    {
        const auto it = m_sharedMaterials.find(std::string(name));
        if (it != m_sharedMaterials.end())
            return it->second.get();
        return nullptr;
    }

    bool AssetManager::Reload(const std::filesystem::path& path)
    {
        const std::filesystem::path key = path.lexically_normal();
        if (const auto it = m_shaders.find(key); it != m_shaders.end())
            return it->second->Reload();
        NS_LOG_WARN(Graphics, "AssetManager::Reload: 未キャッシュの path: {}", key.string());
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
        NS_LOG_INFO(Graphics, "AssetManager: shader reload {} / {} 本成功", reloaded, m_shaders.size());
        return reloaded;
    }

    void AssetManager::Clear() noexcept
    {
        // material は leaf である shader / texture を参照するので先に解放する
        m_sharedMaterials.clear();
        m_materials.clear();
        m_textures.clear();
        m_shaders.clear();
        m_meshes.clear();
        m_skinnedModels.clear();
        m_animationSources.clear();
        m_boundClips.clear();
        m_builtins.clear();
    }
} // namespace NS::Object
