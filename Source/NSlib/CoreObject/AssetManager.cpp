#include "NSlib/CoreObject/AssetManager.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Reflection/SubObjectEntry.h"
#include "NSlib/Graphics/FrameConstants.h"
#include "NSlib/Graphics/GltfLoader.h"
#include "NSlib/Graphics/Material.h"
#include "NSlib/Graphics/Mesh.h"
#include "NSlib/Graphics/MeshPrimitives.h"
#include "NSlib/Graphics/Retarget.h"
#include "NSlib/Graphics/Shader.h"
#include "NSlib/Graphics/SkeletalMesh.h"
#include "NSlib/Graphics/StaticMesh.h"
#include "NSlib/Graphics/Texture.h"
#include "NSlib/Physics/MeshShape.h"
#include "NSlib/Windows/Filesystem.h"

#include "NSlib/Core/Types.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string_view>
#include <vector>

// json.hpp は /W4 で警告が出るため、この翻訳単位でだけ警告を抑止して取り込む
#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

namespace NS
{
    std::optional<std::string> ResolveContentPath(const std::string& relative)
    {
        // ディレクトリトラバーサルの防止は ResolveUnder が持つ
        return OS::FileSystem::ResolveUnder(OS::FileSystem::ContentRoot(), relative);
    }

    namespace
    {
        // 描画の RegisterBuiltins と当たりの LoadMeshShape が同じ表から形を作る
        // 組み込みの形は両者で食い違わない
        struct BuiltinShape
        {
            const char* name;
            Gfx::MeshGeometry (*make)();
        };

        constexpr std::array<BuiltinShape, 7> k_BuiltinShapes = {{
            {"cube", [] { return Gfx::MakeCube(Vector3{0.5f, 0.5f, 0.5f}); }},
            {"sphere", [] { return Gfx::MakeSphere(0.5f); }},
            {"wedge45", [] { return Gfx::MakeSlope(45.0f, Vector3{0.5f, 0.5f, 0.5f}); }},
            {"wedge30", [] { return Gfx::MakeSlope(30.0f, Vector3{0.5f, 0.5f, 0.5f}); }},
            {"wedge22", [] { return Gfx::MakeSlope(22.5f, Vector3{0.5f, 0.5f, 0.5f}); }},
            {"wedge15", [] { return Gfx::MakeSlope(15.0f, Vector3{0.5f, 0.5f, 0.5f}); }},
            {"shadowQuad", [] { return Gfx::MakePlane(Vector2{0.5f, 0.5f}); }},
        }};

        // RegisterSharedMaterials が登録する名前。登録を足したらここにも足す
        constexpr std::array<std::string_view, 3> k_SharedMaterialNames = {"player", "water", "shadow"};

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

        [[nodiscard]] bool IsSharedMaterialName(std::string_view name) noexcept
        {
            return std::find(k_SharedMaterialNames.begin(), k_SharedMaterialNames.end(), name) !=
                   k_SharedMaterialNames.end();
        }

        // index の並びを入れ替えずに写す。描画の並びのまま (v1 - v0) × (v2 - v0) が表面の外を向く
        // 当たりの表裏もこの向きで決まる
        [[nodiscard]] std::vector<Phys::Triangle> MakeTriangles(const Gfx::MeshGeometry& geom)
        {
            std::vector<Phys::Triangle> triangles;
            triangles.reserve(geom.indices.size() / 3);
            const std::size_t vertexCount = geom.vertices.size();
            for (std::size_t i = 0; i + 2 < geom.indices.size(); i += 3)
            {
                const uint32 a = geom.indices[i];
                const uint32 b = geom.indices[i + 1];
                const uint32 c = geom.indices[i + 2];
                if (a >= vertexCount || b >= vertexCount || c >= vertexCount)
                {
                    continue;
                }

                triangles.push_back(Phys::Triangle{
                    geom.vertices[a].position, geom.vertices[b].position, geom.vertices[c].position});
            }
            return triangles;
        }

        // Jolt の形は当たりを頼まれた時に 1 度だけ作る。描画だけの mesh には作らない
        Phys::MeshShape* WithShape(Phys::MeshShape* collision)
        {
            if (collision != nullptr && collision->shape == nullptr)
            {
                collision->shape = Phys::CreateMeshShape(collision->triangles);
            }
            return collision;
        }

        // geom はこの呼出中のみ参照される
        [[nodiscard]] std::unique_ptr<Gfx::StaticMesh> MakeStaticMesh(const Gfx::MeshGeometry& geom)
        {
            Gfx::MeshDesc desc{};
            desc.vertices = geom.vertices.data();
            desc.vertexCount = geom.vertices.size();
            desc.indices = geom.indices.data();
            desc.indexCount = geom.indices.size();
            if (geom.hasBounds)
            {
                desc.precomputedBounds = &geom.bounds;
            }
            return Gfx::StaticMesh::Create(desc);
        }

        [[nodiscard]] Gfx::BlendMode ParseBlend(const std::string& value) noexcept
        {
            if (value == "Alpha")
            {
                return Gfx::BlendMode::Alpha;
            }
            if (value == "Additive")
            {
                return Gfx::BlendMode::Additive;
            }
            return Gfx::BlendMode::Opaque;
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

        out.baseColor = FieldVector3(j, "baseColor", Vector3{1.0f, 1.0f, 1.0f});

        out.blend = Gfx::BlendMode::Opaque;
        if (j.contains("blend") && j["blend"].is_string())
        {
            out.blend = ParseBlend(j["blend"].get<std::string>());
        }

        outError.clear();
        return true;
    }

    AssetManager::AssetManager(std::string baseDir) noexcept : m_baseDir(std::move(baseDir)) {}
    AssetManager::~AssetManager() = default;

    namespace
    {
        AssetManager* g_currentAssets = nullptr;
    } // namespace

    AssetManager& AssetManager::Get() noexcept
    {
        if (g_currentAssets != nullptr)
        {
            return *g_currentAssets;
        }
        // 差していない間に返す。RegisterBuiltins と RegisterSharedMaterials は呼んでいない
        static AssetManager s_unregistered{OS::FileSystem::ContentRoot()};
        return s_unregistered;
    }

    AssetManagerScope::AssetManagerScope(AssetManager& assets) noexcept : m_previous(g_currentAssets)
    {
        g_currentAssets = &assets;
    }

    AssetManagerScope::~AssetManagerScope() noexcept
    {
        g_currentAssets = m_previous;
    }

    Gfx::Shader* AssetManager::GetOrLoadShader(std::string_view path)
    {
        // 区切り文字や . / .. の表記揺れで同一ファイルが別キー扱いにならないよう正規化してから重複をまとめる
        const std::string key = OS::FileSystem::Normalize(path);
        if (const std::map<std::string, std::unique_ptr<Gfx::Shader>>::iterator it = m_shaders.find(key);
            it != m_shaders.end())
        {
            return it->second.get();
        }

        std::unique_ptr<Gfx::Shader> shader = Gfx::Shader::Create(key);
        Gfx::Shader* raw = shader.get();
        if (raw->IsUsingFallback())
        {
            NS_LOG_ERROR(Graphics, "AssetManager: shader の読込/コンパイル失敗、 fallback 描画: {}", key);
        }
        m_shaders.emplace(key, std::move(shader));
        return raw;
    }

    Gfx::Texture* AssetManager::GetOrLoadTexture(std::string_view path)
    {
        const std::string key = OS::FileSystem::Normalize(path);
        if (const std::map<std::string, std::unique_ptr<Gfx::Texture>>::iterator it = m_textures.find(key);
            it != m_textures.end())
        {
            return it->second.get();
        }
        std::unique_ptr<Gfx::Texture> texture = Gfx::Texture::Create({.path = key});
        Gfx::Texture* raw = texture.get();
        m_textures.emplace(key, std::move(texture));
        return raw;
    }

    AssetManager::MeshRecord& AssetManager::LoadMeshRecord(std::string_view path)
    {
        const std::string key = OS::FileSystem::Normalize(path);
        if (const std::map<std::string, MeshRecord>::iterator it = m_meshes.find(key); it != m_meshes.end())
        {
            return it->second;
        }

        // 描画と当たりのどちらを先に頼まれても両方ここで作る
        // 当たりは MeshCollision が描画と同じ参照で頼む。当たりが先でも GPU mesh は描画に使われる
        MeshRecord record;
        const Gfx::MeshGeometry geom = Gfx::LoadGltfMesh(key);
        if (geom.vertices.empty() || geom.indices.empty())
        {
            NS_LOG_WARN(Graphics, "AssetManager: mesh の読込失敗 / 空: {}", key);
        }
        else
        {
            std::unique_ptr<Gfx::StaticMesh> mesh = MakeStaticMesh(geom);
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
            record.collision = std::make_unique<Phys::MeshShape>();
            record.collision->triangles = MakeTriangles(geom);
        }
        // 失敗した記録も残し、同じ参照を持つ配置物が毎回ディスクを読むのを防ぐ。修正後の再試行は Clear() で解いてから
        return m_meshes.emplace(key, std::move(record)).first->second;
    }

    Gfx::Mesh* AssetManager::LoadMesh(std::string_view name)
    {
        if (Gfx::Mesh* mesh = TryLoadMesh(name))
        {
            return mesh;
        }
        // 空の名前は「メッシュを選んでいない」で、誤りではない
        // 組み込みの形の名前は装置が無くて登録されていない時だけ外れる
        if (!name.empty() && FindBuiltinShape(name) == nullptr)
        {
            NS_LOG_WARN(Scene, "メッシュ {} を読めないので cube で描く", name);
        }
        return Builtin("cube");
    }

    Gfx::Mesh* AssetManager::TryLoadMesh(std::string_view name)
    {
        if (name.empty())
        {
            return nullptr;
        }
        // 組み込みの形の名前はファイルでないので、登録の前でもファイルとして読まない
        if (FindBuiltinShape(name) != nullptr)
        {
            return Builtin(name);
        }
        const std::optional<std::string> path = ResolveContentPath(std::string{name});
        if (!path)
        {
            return nullptr;
        }
        return LoadMeshRecord(*path).mesh.get();
    }

    const Phys::MeshShape* AssetManager::LoadMeshShape(std::string_view name)
    {
        // 描く物と同じく、空の名前は cube にする
        std::string_view shapeName = name;
        if (shapeName.empty())
        {
            shapeName = "cube";
        }
        if (const BuiltinShape* shape = FindBuiltinShape(shapeName))
        {
            const std::string key{shapeName};
            std::map<std::string, std::unique_ptr<Phys::MeshShape>>::iterator it = m_builtinCollisions.find(key);
            if (it == m_builtinCollisions.end())
            {
                std::unique_ptr<Phys::MeshShape> collision = std::make_unique<Phys::MeshShape>();
                collision->triangles = MakeTriangles(shape->make());
                it = m_builtinCollisions.emplace(key, std::move(collision)).first;
            }
            return WithShape(it->second.get());
        }

        const Phys::MeshShape* collision = nullptr;
        if (const std::optional<std::string> path = ResolveContentPath(std::string{name}))
        {
            collision = WithShape(LoadMeshRecord(*path).collision.get());
        }
        if (collision == nullptr)
        {
            NS_LOG_WARN(Scene, "メッシュ {} の当たりを読めないので cube にする", name);
            return LoadMeshShape("cube");
        }
        return collision;
    }

    const SkinnedModel* AssetManager::LoadSkinnedModel(std::string_view name)
    {
        if (name.empty())
        {
            return nullptr;
        }
        const std::optional<std::string> path = ResolveContentPath(std::string{name});
        if (!path)
        {
            NS_LOG_WARN(Scene, "骨付きのモデル {} は ContentRoot の外を指すので読まない", name);
            return nullptr;
        }
        return LoadSkinnedModelFile(*path);
    }

    const SkinnedModel* AssetManager::LoadSkinnedModelFile(std::string_view path)
    {
        const std::string key = OS::FileSystem::Normalize(path);
        if (const std::map<std::string, SkinnedModel>::iterator it = m_skinnedModels.find(key);
            it != m_skinnedModels.end())
        {
            return &it->second;
        }

        Gfx::SkinnedMeshData data = Gfx::LoadGltfSkinnedMesh(key);
        if (!data.IsValid())
        {
            NS_LOG_ERROR(Graphics, "AssetManager: skinned glTF 読込失敗: {}", key);
            return nullptr;
        }

        if (data.vertices.empty())
        {
            NS_LOG_ERROR(Graphics, "AssetManager: skinned mesh に頂点が無い: {}", key);
            return nullptr;
        }

        Gfx::SkinnedMeshDesc smd{};
        smd.vertices = data.vertices.data();
        smd.vertexCount = data.vertices.size();
        smd.indices = data.indices.data();
        smd.indexCount = data.indices.size();
        smd.boneCount = data.skeleton.BoneCount();

        SkinnedModel model{};
        model.mesh = Gfx::SkeletalMesh::Create(smd);
        if (model.mesh == nullptr || !model.mesh->IsValid())
        {
            // GPU buffer 生成に失敗。壊れた mesh をキャッシュせず無効を返し、Draw が無音で何もしないのを防ぐ
            NS_LOG_ERROR(Graphics, "AssetManager: skinned mesh の GPU 生成失敗: {}", key);
            return nullptr;
        }

        model.skeleton = std::move(data.skeleton);
        model.clips = std::move(data.animations);
        return &m_skinnedModels.emplace(key, std::move(model)).first->second;
    }

    const Gfx::AnimationSource* AssetManager::GetOrLoadAnimationSource(std::string_view path)
    {
        const std::string key = OS::FileSystem::Normalize(path);
        // 空の項目は読み込み失敗の記録。get() が nullptr を返し、再読み込みを避ける
        if (const std::map<std::string, std::unique_ptr<Gfx::AnimationSource>>::iterator it =
                m_animationSources.find(key);
            it != m_animationSources.end())
        {
            return it->second.get();
        }

        Gfx::AnimationSource source = Gfx::LoadGltfAnimationSource(key);
        if (!source.IsValid())
        {
            NS_LOG_WARN(Graphics, "AssetManager: アニメーション glTF の読込失敗 / 空: {}", key);
            // 壊れた path を負キャッシュし、毎回ディスクを読むのを防ぐ。再試行は Clear() から
            m_animationSources.emplace(key, nullptr);
            return nullptr;
        }

        std::unique_ptr<Gfx::AnimationSource> owned = std::make_unique<Gfx::AnimationSource>(std::move(source));
        const Gfx::AnimationSource* raw = owned.get();
        m_animationSources.emplace(key, std::move(owned));
        return raw;
    }

    const std::vector<Gfx::AnimationClip>* AssetManager::GetOrLoadBoundClips(std::string_view clipPath,
                                                                                 std::string_view modelPath)
    {
        const std::pair<std::string, std::string> key{OS::FileSystem::Normalize(clipPath),
                                                      OS::FileSystem::Normalize(modelPath)};
        if (const std::map<std::pair<std::string, std::string>,
                           std::unique_ptr<std::vector<Gfx::AnimationClip>>>::iterator it = m_boundClips.find(key);
            it != m_boundClips.end())
        {
            return it->second.get();
        }

        const Gfx::AnimationSource* source = GetOrLoadAnimationSource(key.first);
        if (source == nullptr)
        {
            // クリップ側の読込失敗は上流で負キャッシュ済み。組としても負キャッシュする
            m_boundClips.emplace(key, nullptr);
            return nullptr;
        }

        const SkinnedModel* model = LoadSkinnedModelFile(key.second);
        if (model == nullptr)
        {
            // model 側の失敗は LoadSkinnedModelFile がキャッシュせず再試行できるようにしている。組で恒久化しない
            return nullptr;
        }

        // 結合で index を振り直した複製は避けられない派生データだが、所有はこちら側なので
        // 同じ組で解決する全インスタンスがこの 1 本を共有する
        std::unique_ptr<std::vector<Gfx::AnimationClip>> bound =
            std::make_unique<std::vector<Gfx::AnimationClip>>(
                Gfx::BindClipsByName(source->animations, source->skeleton, model->skeleton));
        const std::vector<Gfx::AnimationClip>* raw = bound.get();
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

    Gfx::StaticMesh* AssetManager::Builtin(std::string_view name) const noexcept
    {
        const std::map<std::string, std::unique_ptr<Gfx::StaticMesh>>::const_iterator it =
            m_builtins.find(std::string(name));
        if (it != m_builtins.end())
        {
            return it->second.get();
        }
        return nullptr;
    }

    Gfx::StaticMesh* AssetManager::GetOrMakeCapsuleMesh(float radius, float halfHeight)
    {
        // 非数を鍵にすると map の比較が順序にならず、引き当てが壊れる。無限大は頂点の座標が無限大になり描けない
        if (!std::isfinite(radius) || !std::isfinite(halfHeight))
        {
            NS_LOG_WARN(
                Graphics, "AssetManager: カプセルの寸法が有限でない (半径 {}, 半分の高さ {})", radius, halfHeight);
            return nullptr;
        }

        const std::pair<float, float> key{radius, halfHeight};
        if (const std::map<std::pair<float, float>, std::unique_ptr<Gfx::StaticMesh>>::iterator it =
                m_capsules.find(key);
            it != m_capsules.end())
        {
            return it->second.get();
        }

        std::unique_ptr<Gfx::StaticMesh> mesh = MakeStaticMesh(Gfx::MakeCapsule(radius, halfHeight));
        // device の無い間に作った空の mesh を覚えると、device ができた後も空のまま返し続ける
        if (mesh == nullptr || !mesh->IsValid())
        {
            NS_LOG_ERROR(
                Graphics, "AssetManager: カプセルの mesh の GPU 生成失敗 (半径 {}, 半分の高さ {})", radius, halfHeight);
            return nullptr;
        }
        Gfx::StaticMesh* raw = mesh.get();
        m_capsules.emplace(key, std::move(mesh));
        return raw;
    }

    Gfx::Material* AssetManager::LoadMaterial(std::string_view name)
    {
        if (Gfx::Material* material = TryLoadMaterial(name))
        {
            return material;
        }
        // 空の名前は「マテリアルを選んでいない」で、誤りではない
        // 共有の名前は装置が無くて登録されていない時だけ外れる
        if (!name.empty() && !IsSharedMaterialName(name))
        {
            NS_LOG_WARN(Scene, "マテリアル {} を読めないので共有の player で描く", name);
        }
        return SharedMaterial("player");
    }

    Gfx::Material* AssetManager::TryLoadMaterial(std::string_view name)
    {
        if (Gfx::Material* shared = SharedMaterial(name))
        {
            return shared;
        }
        // 共有の名前はファイルでないので、登録の前でもファイルとして読まない
        if (name.empty() || IsSharedMaterialName(name))
        {
            return nullptr;
        }
        const std::optional<std::string> path = ResolveContentPath(std::string{name});
        if (!path)
        {
            return nullptr;
        }
        if (const MaterialRecord* record = LoadMaterialFile(*path))
        {
            return record->material.get();
        }
        return nullptr;
    }

    Vector3 AssetManager::MaterialBaseColor(std::string_view name) const
    {
        const Vector3 white{1.0f, 1.0f, 1.0f};
        if (name.empty() || IsSharedMaterialName(name))
        {
            return white;
        }
        const std::optional<std::string> path = ResolveContentPath(std::string{name});
        if (!path)
        {
            return white;
        }
        const std::map<std::string, MaterialRecord>::const_iterator it =
            m_materials.find(OS::FileSystem::Normalize(*path));
        if (it == m_materials.end())
        {
            return white;
        }
        return it->second.baseColor;
    }

    const AssetManager::MaterialRecord* AssetManager::LoadMaterialFile(const std::string& path)
    {
        const std::string matKey = OS::FileSystem::Normalize(path);
        if (const std::map<std::string, MaterialRecord>::iterator it = m_materials.find(matKey);
            it != m_materials.end())
        {
            return &it->second;
        }

        const std::optional<std::string> textOpt = OS::FileSystem::ReadAllText(matKey);
        if (!textOpt)
        {
            NS_LOG_ERROR(Graphics, "AssetManager: .mat 読込失敗: {}", matKey);
            return nullptr;
        }

        MaterialFileDesc fileDesc{};
        std::string parseError;
        if (!ParseMaterialJson(*textOpt, fileDesc, parseError))
        {
            NS_LOG_ERROR(Graphics, "AssetManager: .mat 解析失敗 ({}): {}", parseError, matKey);
            return nullptr;
        }

        // 相対 path は構築時の baseDir 基準で解決する。絶対 / ドライブ相対は Combine が baseDir を捨てて通す
        const auto resolve = [this](std::string_view p) { return OS::FileSystem::Combine(m_baseDir, p); };

        Gfx::Shader* vertexShader = GetOrLoadShader(resolve(fileDesc.vertexShader));
        Gfx::Shader* pixelShader = GetOrLoadShader(resolve(fileDesc.pixelShader));

        // 定数バッファは slot 0 で Model が流す FrameCB に合わせる
        Gfx::MaterialDesc matDesc{};
        matDesc.vertexShader = vertexShader;
        matDesc.pixelShader = pixelShader;
        matDesc.constantBufferSize = sizeof(Gfx::FrameCB);
        matDesc.cbSlot = 0;
        matDesc.blend = fileDesc.blend;
        std::unique_ptr<Gfx::Material> material = Gfx::Material::Create(matDesc);

        for (std::size_t i = 0; i < fileDesc.textures.size(); ++i)
        {
            Gfx::Texture* texture = GetOrLoadTexture(resolve(fileDesc.textures[i]));
            material->SetTexture(static_cast<uint>(i), texture);
        }

        return &m_materials.emplace(matKey, MaterialRecord{std::move(material), fileDesc.baseColor}).first->second;
    }

    void AssetManager::RegisterSharedMaterials()
    {
        const auto shaderPath = [this](const char* name) {
            return OS::FileSystem::Combine(OS::FileSystem::Combine(m_baseDir, "Shaders"), name);
        };
        Gfx::Shader* standardVS = GetOrLoadShader(shaderPath("standard.vs.hlsl"));
        Gfx::Shader* playerPS = GetOrLoadShader(shaderPath("player.ps.hlsl"));
        Gfx::Texture* baseTexture = GetOrLoadTexture(OS::FileSystem::Combine(
            OS::FileSystem::Combine(OS::FileSystem::Combine(m_baseDir, "Assets"), "Textures"),
            "cube_test.png"));

        // 定数バッファは slot 0 で Model が流す FrameCB に合わせる
        Gfx::MaterialDesc base{};
        base.vertexShader = standardVS;
        base.pixelShader = playerPS;
        base.constantBufferSize = sizeof(Gfx::FrameCB);
        base.cbSlot = 0;

        // player: 単一 Texture2D。slot0 に基準テクスチャを bind する
        {
            std::unique_ptr<Gfx::Material> mat = Gfx::Material::Create(base);
            mat->SetTexture(0, baseTexture);
            m_sharedMaterials.emplace("player", std::move(mat));
        }
        // water: alpha<1 を出す water.ps + Alpha ブレンドの専用 material
        {
            Gfx::MaterialDesc desc = base;
            desc.pixelShader = GetOrLoadShader(shaderPath("water.ps.hlsl"));
            desc.blend = Gfx::BlendMode::Alpha;
            std::unique_ptr<Gfx::Material> mat = Gfx::Material::Create(desc);
            mat->SetTexture(0, baseTexture);
            m_sharedMaterials.emplace("water", std::move(mat));
        }
        // shadow: shadow.ps が放射状アルファを生成するためテクスチャ不要、Alpha ブレンド
        {
            Gfx::MaterialDesc desc = base;
            desc.pixelShader = GetOrLoadShader(shaderPath("shadow.ps.hlsl"));
            desc.blend = Gfx::BlendMode::Alpha;
            m_sharedMaterials.emplace("shadow", Gfx::Material::Create(desc));
        }
    }

    Gfx::Material* AssetManager::SharedMaterial(std::string_view name) const noexcept
    {
        const std::map<std::string, std::unique_ptr<Gfx::Material>>::const_iterator it =
            m_sharedMaterials.find(std::string(name));
        if (it != m_sharedMaterials.end())
        {
            return it->second.get();
        }
        return nullptr;
    }

    bool AssetManager::Reload(std::string_view path)
    {
        const std::string key = OS::FileSystem::Normalize(path);
        if (const std::map<std::string, std::unique_ptr<Gfx::Shader>>::iterator it = m_shaders.find(key);
            it != m_shaders.end())
        {
            return it->second->Reload();
        }
        NS_LOG_WARN(Graphics, "AssetManager::Reload: 未キャッシュの path: {}", key);
        return false;
    }

    std::size_t AssetManager::ReloadAllShaders()
    {
        std::size_t reloaded = 0;
        for (std::pair<const std::string, std::unique_ptr<Gfx::Shader>>& entry : m_shaders)
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
        m_capsules.clear();
    }
} // namespace NS
