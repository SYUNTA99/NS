#include "Runtime/Object/AssetManager.h"

#include "Runtime/Core/Logger.h"
#include "Runtime/Core/Math.h"
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
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Physics/MeshCollision.h"
#include "Runtime/Platform/Filesystem.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

// json.hpp は /W4 で警告が出るため、この翻訳単位でだけ警告を抑止して取り込む
#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

namespace NS::Obj
{
    std::optional<std::string> ResolveContentPath(const std::string& relative)
    {
        // ディレクトリトラバーサルの防止は ResolveUnder が持つ
        return NS::Platform::FileSystem::ResolveUnder(NS::Platform::FileSystem::ContentRoot(), relative);
    }

    NS::Gfx::Mesh* ResolveMeshFromRef(AssetManager& assets, const std::string& meshRef)
    {
        if (meshRef.empty())
            return nullptr;

        if (NS::Gfx::StaticMesh* builtin = assets.Builtin(meshRef))
        {
            return builtin;
        }

        const std::optional<std::string> resolved = ResolveContentPath(meshRef);
        if (!resolved)
        {
            return nullptr;
        }
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

        // 描画の RegisterBuiltins と当たりの GetOrLoadMeshCollision が同じ表から形を作る
        // 組み込みの形は両者で食い違わない
        struct BuiltinShape
        {
            const char* name;
            NS::Gfx::MeshGeometry (*make)();
        };

        constexpr std::array<BuiltinShape, 7> k_BuiltinShapes = {{
            {k_BuiltinCube, [] { return NS::Gfx::MakeCube(NS::Core::Vector3{0.5f, 0.5f, 0.5f}); }},
            {k_BuiltinSphere, [] { return NS::Gfx::MakeSphere(0.5f); }},
            {k_BuiltinWedge45, [] { return NS::Gfx::MakeSlope(45.0f, NS::Core::Vector3{0.5f, 0.5f, 0.5f}); }},
            {k_BuiltinWedge30, [] { return NS::Gfx::MakeSlope(30.0f, NS::Core::Vector3{0.5f, 0.5f, 0.5f}); }},
            {k_BuiltinWedge22, [] { return NS::Gfx::MakeSlope(22.5f, NS::Core::Vector3{0.5f, 0.5f, 0.5f}); }},
            {k_BuiltinWedge15, [] { return NS::Gfx::MakeSlope(15.0f, NS::Core::Vector3{0.5f, 0.5f, 0.5f}); }},
            {k_BuiltinShadowQuad, [] { return NS::Gfx::MakePlane(NS::Core::Vector2{0.5f, 0.5f}); }},
        }};

        // 引く先は m_builtins でなく k_BuiltinShapes。RegisterBuiltins を呼んでいなくても引ける
        [[nodiscard]] const BuiltinShape* FindBuiltinShape(std::string_view name) noexcept
        {
            for (const BuiltinShape& shape : k_BuiltinShapes)
            {
                if (name == shape.name)
                {
                    return &shape;
                }
            }
            return nullptr;
        }

        // index の並びを入れ替えずに写す。描画の並びのまま (v1 - v0) × (v2 - v0) が表面の外を向く
        // 当たりの表裏もこの向きで決まる
        [[nodiscard]] std::vector<NS::Phys::Triangle> MakeTriangles(const NS::Gfx::MeshGeometry& geom)
        {
            std::vector<NS::Phys::Triangle> triangles;
            triangles.reserve(geom.indices.size() / 3);
            const std::size_t vertexCount = geom.vertices.size();
            for (std::size_t i = 0; i + 2 < geom.indices.size(); i += 3)
            {
                const std::uint32_t a = geom.indices[i];
                const std::uint32_t b = geom.indices[i + 1];
                const std::uint32_t c = geom.indices[i + 2];
                if (a >= vertexCount || b >= vertexCount || c >= vertexCount)
                {
                    continue;
                }

                triangles.push_back(NS::Phys::Triangle{
                    geom.vertices[a].position, geom.vertices[b].position, geom.vertices[c].position});
            }
            return triangles;
        }

        // Jolt の形は当たりを頼まれた時に 1 度だけ作る。描画だけの mesh には作らない
        NS::Phys::MeshCollision* WithShape(NS::Phys::MeshCollision* collision)
        {
            if (collision != nullptr && collision->shape == nullptr)
            {
                collision->shape = NS::Phys::CreateMeshShape(collision->triangles);
            }
            return collision;
        }

        // geom はこの呼出中のみ参照される
        [[nodiscard]] std::unique_ptr<NS::Gfx::StaticMesh> MakeStaticMesh(const NS::Gfx::MeshGeometry& geom)
        {
            NS::Gfx::MeshDesc desc{};
            desc.vertices = geom.vertices.data();
            desc.vertexCount = geom.vertices.size();
            desc.indices = geom.indices.data();
            desc.indexCount = geom.indices.size();
            if (geom.hasBounds)
            {
                desc.precomputedBounds = &geom.bounds;
            }
            return NS::Gfx::StaticMesh::Create(desc);
        }

        [[nodiscard]] NS::Gfx::BlendMode ParseBlend(const std::string& value) noexcept
        {
            if (value == "Alpha")
            {
                return NS::Gfx::BlendMode::Alpha;
            }
            if (value == "Additive")
            {
                return NS::Gfx::BlendMode::Additive;
            }
            return NS::Gfx::BlendMode::Opaque;
        }
    } // namespace

    bool ParseMaterialJson(std::string_view jsonText, MaterialFileDesc& out, std::string& outError)
    {
        // 例外を投げない parse。不正 JSON は is_discarded() で検知する
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
            for (const nlohmann::json& tex : j["textures"])
            {
                if (tex.is_string())
                {
                    out.textures.emplace_back(tex.get<std::string>());
                }
            }
        }

        // baseColor は任意の 3 要素
        out.baseColor = NS::Core::Vector3{1.0f, 1.0f, 1.0f};
        if (j.contains("baseColor") && j["baseColor"].is_array() && j["baseColor"].size() == 3)
        {
            const nlohmann::json& c = j["baseColor"];
            if (c[0].is_number() && c[1].is_number() && c[2].is_number())
            {
                out.baseColor = NS::Core::Vector3{c[0].get<float>(), c[1].get<float>(), c[2].get<float>()};
            }
        }

        // blend は任意
        out.blend = NS::Gfx::BlendMode::Opaque;
        if (j.contains("blend") && j["blend"].is_string())
        {
            out.blend = ParseBlend(j["blend"].get<std::string>());
        }

        outError.clear();
        return true;
    }

    AssetManager::AssetManager(std::string baseDir) noexcept : m_baseDir(std::move(baseDir)) {}
    AssetManager::~AssetManager() = default;

    NS::Gfx::Shader* AssetManager::GetOrLoadShader(std::string_view path)
    {
        // 区切り文字や . / .. の表記揺れで同一ファイルが別キー扱いにならないよう正規化してから重複をまとめる
        const std::string key = NS::Platform::FileSystem::Normalize(path);
        if (const std::map<std::string, std::unique_ptr<NS::Gfx::Shader>>::iterator it = m_shaders.find(key); it != m_shaders.end())
        {
            return it->second.get();
        }

        std::unique_ptr<NS::Gfx::Shader> shader = NS::Gfx::Shader::Create(key);
        NS::Gfx::Shader* raw = shader.get();
        if (raw->IsUsingFallback())
        {
            NS_LOG_ERROR(Graphics, "AssetManager: shader の読込/コンパイル失敗、 fallback 描画: {}", key);
        }
        m_shaders.emplace(key, std::move(shader));
        return raw;
    }

    NS::Gfx::Texture* AssetManager::GetOrLoadTexture(std::string_view path)
    {
        const std::string key = NS::Platform::FileSystem::Normalize(path);
        if (const std::map<std::string, std::unique_ptr<NS::Gfx::Texture>>::iterator it = m_textures.find(key); it != m_textures.end())
        {
            return it->second.get();
        }
        std::unique_ptr<NS::Gfx::Texture> texture = NS::Gfx::Texture::Create({.path = key});
        NS::Gfx::Texture* raw = texture.get();
        m_textures.emplace(key, std::move(texture));
        return raw;
    }

    AssetManager::MeshRecord& AssetManager::LoadMeshRecord(std::string_view path)
    {
        const std::string key = NS::Platform::FileSystem::Normalize(path);
        if (const std::map<std::string, MeshRecord>::iterator it = m_meshes.find(key); it != m_meshes.end())
        {
            return it->second;
        }

        // 描画と当たりのどちらを先に頼まれても両方ここで作る
        // 当たりは MeshCollider が描画と同じ参照で頼む。当たりが先でも GPU mesh は描画に使われる
        MeshRecord record;
        const NS::Gfx::MeshGeometry geom = NS::Gfx::LoadGltfMesh(key);
        if (geom.vertices.empty() || geom.indices.empty())
        {
            NS_LOG_WARN(Graphics, "AssetManager: mesh の読込失敗 / 空: {}", key);
        }
        else
        {
            std::unique_ptr<NS::Gfx::StaticMesh> mesh = MakeStaticMesh(geom);
            if (mesh == nullptr || !mesh->IsValid())
            {
                NS_LOG_ERROR(
                    Graphics,
                    "AssetManager: mesh の GPU 生成失敗。 描画は cube へフォールバックし、 当たりは本物の形のまま: {}",
                    key);
            }
            else
            {
                record.mesh = std::move(mesh);
            }
            // GPU 生成だけ失敗しても当たりは作る。device 無しのテストでも当たりを確かめられる
            // 当たりだけ頼まれた時も GPU 生成を通るので、device の無いテストでは上のエラーが出る
            // TODO: コライダーの無い描画だけの mesh も三角形を Clear() まで持つ
            // 大きな mesh を飾りに多く置いてメモリが効いてきたら、当たりを頼まれた時に作る形へ移す
            record.collision = std::make_unique<NS::Phys::MeshCollision>();
            record.collision->triangles = MakeTriangles(geom);
        }
        // 失敗した記録も残し、同じ参照を持つ配置物が毎回ディスクを読むのを防ぐ。修正後の再試行は Clear() で解いてから
        return m_meshes.emplace(key, std::move(record)).first->second;
    }

    NS::Gfx::Mesh* AssetManager::GetOrLoadMesh(std::string_view path)
    {
        return LoadMeshRecord(path).mesh.get();
    }

    std::size_t AssetManager::MeshCacheSize() const noexcept
    {
        return m_meshes.size();
    }

    const NS::Phys::MeshCollision* AssetManager::GetOrLoadMeshCollision(const std::string& meshRef)
    {
        if (meshRef.empty())
        {
            return nullptr;
        }

        if (const BuiltinShape* shape = FindBuiltinShape(meshRef))
        {
            std::map<std::string, std::unique_ptr<NS::Phys::MeshCollision>>::iterator it = m_builtinCollisions.find(meshRef);
            if (it == m_builtinCollisions.end())
            {
                std::unique_ptr<NS::Phys::MeshCollision> collision = std::make_unique<NS::Phys::MeshCollision>();
                collision->triangles = MakeTriangles(shape->make());
                it = m_builtinCollisions.emplace(meshRef, std::move(collision)).first;
            }
            return WithShape(it->second.get());
        }

        const std::optional<std::string> resolved = ResolveContentPath(meshRef);
        if (!resolved)
        {
            return nullptr;
        }
        return WithShape(LoadMeshRecord(*resolved).collision.get());
    }

    LoadedSkinnedModel AssetManager::GetOrLoadSkinnedModel(std::string_view path)
    {
        const std::string key = NS::Platform::FileSystem::Normalize(path);
        std::map<std::string, SkinnedModelRecord>::iterator it = m_skinnedModels.find(key);
        if (it == m_skinnedModels.end())
        {
            NS::Gfx::SkinnedMeshData data = NS::Gfx::LoadGltfSkinnedMesh(key);
            if (!data.IsValid())
            {
                NS_LOG_ERROR(Graphics, "AssetManager: skinned glTF 読込失敗: {}", key);
                return LoadedSkinnedModel{};
            }

            if (data.vertices.empty())
            {
                NS_LOG_ERROR(Graphics, "AssetManager: skinned mesh に頂点が無い: {}", key);
                return LoadedSkinnedModel{};
            }

            // GPU 生成用の記述子を組む
            NS::Gfx::SkinnedMeshDesc smd{};
            smd.vertices = data.vertices.data();
            smd.vertexCount = data.vertices.size();
            smd.indices = data.indices.data();
            smd.indexCount = data.indices.size();
            smd.boneCount = data.skeleton.BoneCount();

            SkinnedModelRecord record{};
            record.mesh = NS::Gfx::SkeletalMesh::Create(smd);
            if (record.mesh == nullptr || !record.mesh->IsValid())
            {
                // GPU buffer 生成に失敗。壊れた mesh をキャッシュせず無効を返し、Draw が無音で何もしないのを防ぐ
                NS_LOG_ERROR(Graphics, "AssetManager: skinned mesh の GPU 生成失敗: {}", key);
                return LoadedSkinnedModel{};
            }

            record.skeleton = std::move(data.skeleton);
            record.clips = std::move(data.animations);
            it = m_skinnedModels.emplace(key, std::move(record)).first;
        }

        // record への参照を渡す。record は挿入後に書き換えないので、参照は Clear() まで有効
        LoadedSkinnedModel out{};
        out.mesh = it->second.mesh.get();
        out.skeleton = &it->second.skeleton;
        out.clips = &it->second.clips;
        out.valid = true;
        return out;
    }

    const NS::Gfx::AnimationSource* AssetManager::GetOrLoadAnimationSource(std::string_view path)
    {
        const std::string key = NS::Platform::FileSystem::Normalize(path);
        // null エントリは負キャッシュした失敗 path を表す。get() が nullptr を返し再読込を短絡する
        if (const std::map<std::string, std::unique_ptr<NS::Gfx::AnimationSource>>::iterator it = m_animationSources.find(key); it != m_animationSources.end())
        {
            return it->second.get();
        }

        NS::Gfx::AnimationSource source = NS::Gfx::LoadGltfAnimationSource(key);
        if (!source.IsValid())
        {
            NS_LOG_WARN(Graphics, "AssetManager: アニメーション glTF の読込失敗 / 空: {}", key);
            // 壊れた path を負キャッシュし、毎回ディスクを読むのを防ぐ。再試行は Clear() から
            m_animationSources.emplace(key, nullptr);
            return nullptr;
        }

        std::unique_ptr<NS::Gfx::AnimationSource> owned = std::make_unique<NS::Gfx::AnimationSource>(std::move(source));
        const NS::Gfx::AnimationSource* raw = owned.get();
        m_animationSources.emplace(key, std::move(owned));
        return raw;
    }

    const std::vector<NS::Gfx::AnimationClip>* AssetManager::GetOrLoadBoundClips(std::string_view clipPath,
                                                                                 std::string_view modelPath)
    {
        const std::pair<std::string, std::string> key{NS::Platform::FileSystem::Normalize(clipPath),
                                                      NS::Platform::FileSystem::Normalize(modelPath)};
        if (const std::map<std::pair<std::string, std::string>, std::unique_ptr<std::vector<NS::Gfx::AnimationClip>>>::iterator it =
                m_boundClips.find(key);
            it != m_boundClips.end())
        {
            return it->second.get();
        }

        const NS::Gfx::AnimationSource* source = GetOrLoadAnimationSource(key.first);
        if (source == nullptr)
        {
            // クリップ側の読込失敗は上流で負キャッシュ済み。組としても負キャッシュする
            m_boundClips.emplace(key, nullptr);
            return nullptr;
        }

        const LoadedSkinnedModel model = GetOrLoadSkinnedModel(key.second);
        if (!model.valid || model.skeleton == nullptr)
        {
            // model 側の失敗は GetOrLoadSkinnedModel がキャッシュせず再試行できるようにしている。組で恒久化しない
            return nullptr;
        }

        // 結合で index を振り直した複製は避けられない派生データだが、所有はこちら側なので
        // 同じ組で解決する全インスタンスがこの 1 本を共有する
        std::unique_ptr<std::vector<NS::Gfx::AnimationClip>> bound = std::make_unique<std::vector<NS::Gfx::AnimationClip>>(
            NS::Gfx::BindClipsByName(source->animations, source->skeleton, *model.skeleton));
        const std::vector<NS::Gfx::AnimationClip>* raw = bound.get();
        m_boundClips.emplace(key, std::move(bound));
        return raw;
    }

    void AssetManager::RegisterBuiltins()
    {
        for (const BuiltinShape& shape : k_BuiltinShapes)
        {
            m_builtins.emplace(shape.name, MakeStaticMesh(shape.make()));
        }
    }

    NS::Gfx::StaticMesh* AssetManager::Builtin(std::string_view name) const noexcept
    {
        const std::map<std::string, std::unique_ptr<NS::Gfx::StaticMesh>>::const_iterator it = m_builtins.find(std::string(name));
        if (it != m_builtins.end())
        {
            return it->second.get();
        }
        return nullptr;
    }

    LoadedMaterial AssetManager::LoadMaterial(std::string_view matPath)
    {
        const std::string matKey = NS::Platform::FileSystem::Normalize(matPath);
        if (const std::map<std::string, MaterialRecord>::iterator it = m_materials.find(matKey); it != m_materials.end())
        {
            return LoadedMaterial{it->second.material.get(), it->second.baseColor};
        }

        const std::optional<std::string> textOpt = NS::Platform::FileSystem::ReadAllText(matKey);
        if (!textOpt)
        {
            NS_LOG_ERROR(Graphics, "AssetManager: .mat 読込失敗: {}", matKey);
            return LoadedMaterial{};
        }

        // JSON を MaterialFileDesc へ解析する
        MaterialFileDesc fileDesc{};
        std::string parseError;
        if (!ParseMaterialJson(*textOpt, fileDesc, parseError))
        {
            NS_LOG_ERROR(Graphics, "AssetManager: .mat 解析失敗 ({}): {}", parseError, matKey);
            return LoadedMaterial{};
        }

        // 相対 path は構築時の baseDir 基準で解決する。絶対 / ドライブ相対は Combine が baseDir を捨てて通す
        const auto resolve = [this](std::string_view p) { return NS::Platform::FileSystem::Combine(m_baseDir, p); };

        NS::Gfx::Shader* vertexShader = GetOrLoadShader(resolve(fileDesc.vertexShader));
        NS::Gfx::Shader* pixelShader = GetOrLoadShader(resolve(fileDesc.pixelShader));

        // CB は slot 0 で MeshRenderer が流す FrameCB に合わせる
        NS::Gfx::MaterialDesc matDesc{};
        matDesc.vertexShader = vertexShader;
        matDesc.pixelShader = pixelShader;
        matDesc.constantBufferSize = sizeof(NS::Gfx::FrameCB);
        matDesc.cbSlot = 0;
        matDesc.blend = fileDesc.blend;
        std::unique_ptr<NS::Gfx::Material> material = NS::Gfx::Material::Create(matDesc);

        // texture を slot 順に bind する
        for (std::size_t i = 0; i < fileDesc.textures.size(); ++i)
        {
            NS::Gfx::Texture* texture = GetOrLoadTexture(resolve(fileDesc.textures[i]));
            material->SetTexture(static_cast<unsigned>(i), texture);
        }

        MaterialRecord record{};
        record.material = std::move(material);
        record.baseColor = fileDesc.baseColor;
        NS::Gfx::Material* rawMaterial = record.material.get();
        const NS::Core::Vector3 color = record.baseColor;
        m_materials.emplace(matKey, std::move(record));
        return LoadedMaterial{rawMaterial, color};
    }

    void AssetManager::RegisterSharedMaterials()
    {
        const auto shaderPath = [this](const char* name) {
            return NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::Combine(m_baseDir, "Shaders"), name);
        };
        NS::Gfx::Shader* standardVS = GetOrLoadShader(shaderPath("standard.vs.hlsl"));
        NS::Gfx::Shader* playerPS = GetOrLoadShader(shaderPath("player.ps.hlsl"));
        NS::Gfx::Texture* baseTexture = GetOrLoadTexture(NS::Platform::FileSystem::Combine(
            NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::Combine(m_baseDir, "Assets"), "Textures"),
            "cube_test.png"));

        // CB は slot 0 で MeshRenderer が流す FrameCB に合わせる
        NS::Gfx::MaterialDesc base{};
        base.vertexShader = standardVS;
        base.pixelShader = playerPS;
        base.constantBufferSize = sizeof(NS::Gfx::FrameCB);
        base.cbSlot = 0;

        // player: 単一 Texture2D。slot0 に基準テクスチャを bind する
        {
            std::unique_ptr<NS::Gfx::Material> mat = NS::Gfx::Material::Create(base);
            mat->SetTexture(0, baseTexture);
            m_sharedMaterials.emplace(k_SharedPlayer, std::move(mat));
        }
        // water: alpha<1 を出す water.ps + Alpha ブレンドの専用 material
        {
            NS::Gfx::MaterialDesc desc = base;
            desc.pixelShader = GetOrLoadShader(shaderPath("water.ps.hlsl"));
            desc.blend = NS::Gfx::BlendMode::Alpha;
            std::unique_ptr<NS::Gfx::Material> mat = NS::Gfx::Material::Create(desc);
            mat->SetTexture(0, baseTexture);
            m_sharedMaterials.emplace(k_SharedWater, std::move(mat));
        }
        // shadow: shadow.ps が放射状アルファを生成するためテクスチャ不要、Alpha ブレンド
        {
            NS::Gfx::MaterialDesc desc = base;
            desc.pixelShader = GetOrLoadShader(shaderPath("shadow.ps.hlsl"));
            desc.blend = NS::Gfx::BlendMode::Alpha;
            m_sharedMaterials.emplace(k_SharedShadow, NS::Gfx::Material::Create(desc));
        }
    }

    NS::Gfx::Material* AssetManager::SharedMaterial(std::string_view name) const noexcept
    {
        const std::map<std::string, std::unique_ptr<NS::Gfx::Material>>::const_iterator it = m_sharedMaterials.find(std::string(name));
        if (it != m_sharedMaterials.end())
        {
            return it->second.get();
        }
        return nullptr;
    }

    bool AssetManager::Reload(std::string_view path)
    {
        const std::string key = NS::Platform::FileSystem::Normalize(path);
        if (const std::map<std::string, std::unique_ptr<NS::Gfx::Shader>>::iterator it = m_shaders.find(key); it != m_shaders.end())
        {
            return it->second->Reload();
        }
        NS_LOG_WARN(Graphics, "AssetManager::Reload: 未キャッシュの path: {}", key);
        return false;
    }

    std::size_t AssetManager::ReloadAllShaders()
    {
        std::size_t reloaded = 0;
        for (std::pair<const std::string, std::unique_ptr<NS::Gfx::Shader>>& entry : m_shaders)
        {
            if (entry.second->Reload())
            {
                ++reloaded;
            }
        }
        NS_LOG_INFO(Graphics, "AssetManager: shader reload {} / {} 本成功", reloaded, m_shaders.size());
        return reloaded;
    }

    void AssetManager::Clear() noexcept
    {
        // material は shader / texture を参照するので先に解放する
        m_sharedMaterials.clear();
        m_materials.clear();
        m_textures.clear();
        m_shaders.clear();
        m_meshes.clear();
        m_builtinCollisions.clear();
        m_skinnedModels.clear();
        m_animationSources.clear();
        m_boundClips.clear();
        m_builtins.clear();
    }
} // namespace NS::Obj
