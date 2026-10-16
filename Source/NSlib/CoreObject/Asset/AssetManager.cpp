#include "NSlib/CoreObject/Asset/AssetManager.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/Asset/Shader.h"
#include "NSlib/CoreObject/Asset/Texture.h"
#include "NSlib/CoreObject/Reflection/SubObjectEntry.h"
#include "NSlib/Graphics/GltfLoader.h"
#include "NSlib/Graphics/D3DMaterial.h"
#include "NSlib/Graphics/D3DMesh.h"
#include "NSlib/Graphics/MeshPrimitives.h"
#include "NSlib/Graphics/D3DShader.h"
#include "NSlib/Graphics/D3DSkeletalMesh.h"
#include "NSlib/Graphics/D3DStaticMesh.h"
#include "NSlib/Graphics/D3DTexture.h"
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

        constexpr std::array<BuiltinShape, 8> k_BuiltinShapes = {{
            {"cube", [] { return Gfx::MakeCube(Vector3{0.5f, 0.5f, 0.5f}); }},
            {"sphere", [] { return Gfx::MakeSphere(0.5f); }},
            {"capsule", [] { return Gfx::MakeCapsule(0.5f, 0.5f); }},
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
        [[nodiscard]] std::unique_ptr<Gfx::D3DStaticMesh> MakeStaticMesh(const Gfx::MeshGeometry& geom)
        {
            Gfx::D3DStaticMeshDesc desc{};
            desc.vertices = geom.vertices.data();
            desc.vertexCount = geom.vertices.size();
            desc.indices = geom.indices.data();
            desc.indexCount = geom.indices.size();
            if (geom.hasBounds)
            {
                desc.precomputedBounds = &geom.bounds;
            }
            return Gfx::D3DStaticMesh::Create(desc);
        }

        [[nodiscard]] std::unique_ptr<Phys::MeshShape> MakeCollision(const Gfx::MeshGeometry& geom)
        {
            std::unique_ptr<Phys::MeshShape> collision = std::make_unique<Phys::MeshShape>();
            collision->triangles = MakeTriangles(geom);
            return collision;
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
            out.blend = ParseBlend(j["blend"].get<std::string>()).value_or(Gfx::BlendMode::Opaque);
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

    Shader* AssetManager::LoadShader(std::string_view path)
    {
        // 区切り文字や . / .. の表記揺れで同一ファイルが別キー扱いにならないよう正規化してから重複をまとめる
        const std::string key = OS::FileSystem::Normalize(path);
        if (const std::map<std::string, std::unique_ptr<Shader>>::iterator it = m_shaders.find(key);
            it != m_shaders.end())
        {
            return it->second.get();
        }

        std::unique_ptr<Shader> shader = std::make_unique<Shader>(key);
        Shader* raw = shader.get();
        if (raw->D3D()->IsUsingFallback())
        {
            NS_LOG_ERROR(Graphics, "AssetManager: shader の読込/コンパイル失敗、 fallback 描画: {}", key);
        }
        m_shaders.emplace(key, std::move(shader));
        return raw;
    }

    Texture* AssetManager::LoadTexture(std::string_view path)
    {
        const std::string key = OS::FileSystem::Normalize(path);
        if (const std::map<std::string, std::unique_ptr<Texture>>::iterator it = m_textures.find(key);
            it != m_textures.end())
        {
            return it->second.get();
        }
        std::unique_ptr<Texture> texture = std::make_unique<Texture>(key);
        Texture* raw = texture.get();
        m_textures.emplace(key, std::move(texture));
        return raw;
    }

    Mesh& AssetManager::LoadMeshFile(std::string_view path, std::string_view name)
    {
        const std::string key = OS::FileSystem::Normalize(path);
        if (const std::map<std::string, std::unique_ptr<Mesh>>::iterator it = m_meshes.find(key);
            it != m_meshes.end())
        {
            return *it->second;
        }

        // 描画と当たりのどちらを先に頼まれても両方ここで作る
        // 当たりは MeshCollision が描画と同じ参照で頼む。当たりが先でも GPU mesh は描画に使われる
        std::unique_ptr<Gfx::D3DMesh> drawn;
        std::unique_ptr<Phys::MeshShape> collision;
        const Gfx::MeshGeometry geom = Gfx::LoadGltfMesh(key);
        if (geom.vertices.empty() || geom.indices.empty())
        {
            NS_LOG_WARN(Graphics, "AssetManager: mesh の読込失敗 / 空: {}", key);
        }
        else
        {
            std::unique_ptr<Gfx::D3DStaticMesh> mesh = MakeStaticMesh(geom);
            if (mesh == nullptr || !mesh->IsValid())
            {
                NS_LOG_ERROR(
                    Graphics,
                    "AssetManager: mesh の GPU 生成失敗。 描画は cube へフォールバックし、 当たりは本物の形のまま: {}",
                    key);
            }
            else
            {
                drawn = std::move(mesh);
            }
            // GPU 生成だけ失敗しても当たりは作る。device 無しのテストでも当たりを確かめられる
            // 当たりだけ頼まれた時も GPU 生成を通るので、device の無いテストでは上のエラーが出る
            // TODO: コライダーの無い描画だけの mesh も三角形を Clear() まで持つ
            // 大きな mesh を飾りに多く置いてメモリが効いてきたら、当たりを頼まれた時に作る形へ移す
            collision = MakeCollision(geom);
        }
        // 失敗した記録も残し、同じ参照を持つ配置物が毎回ディスクを読むのを防ぐ。修正後の再試行は Clear() で解いてから
        std::unique_ptr<Mesh> mesh =
            std::make_unique<Mesh>(std::string{name}, std::move(drawn), std::move(collision));
        if (mesh->D3D() == nullptr)
        {
            mesh->SetStandIn(&BuiltinMesh("cube"));
        }
        return *m_meshes.emplace(key, std::move(mesh)).first->second;
    }

    Mesh& AssetManager::BuiltinMesh(std::string_view name)
    {
        const std::string key{name};
        std::map<std::string, std::unique_ptr<Mesh>>::iterator it = m_builtins.find(key);
        if (it == m_builtins.end())
        {
            const BuiltinShape* shape = FindBuiltinShape(name);
            NS_ASSERT(Scene, shape != nullptr, "組み込みの形でない名前で組み込みの形を作ろうとしている");
            it = m_builtins.emplace(key, std::make_unique<Mesh>(key, nullptr, MakeCollision(shape->make()))).first;
        }
        return *it->second;
    }

    Mesh& AssetManager::MissingMesh(std::string_view name)
    {
        const std::string key{name};
        std::map<std::string, std::unique_ptr<Mesh>>::iterator it = m_missingMeshes.find(key);
        if (it == m_missingMeshes.end())
        {
            std::unique_ptr<Mesh> mesh = std::make_unique<Mesh>(key, nullptr, nullptr);
            mesh->SetStandIn(&BuiltinMesh("cube"));
            it = m_missingMeshes.emplace(key, std::move(mesh)).first;
        }
        return *it->second;
    }

    Mesh* AssetManager::LoadMesh(std::string_view name)
    {
        // 組み込みの形の名前はファイルでないので、登録の前でもファイルとして読まない
        if (FindBuiltinShape(name) != nullptr)
        {
            return &BuiltinMesh(name);
        }
        Mesh* mesh = nullptr;
        if (!name.empty())
        {
            if (const std::optional<std::string> path = ResolveContentPath(std::string{name}))
            {
                mesh = &LoadMeshFile(*path, name);
            }
        }
        if (mesh == nullptr)
        {
            mesh = &MissingMesh(name);
        }
        // 空の名前は「メッシュを選んでいない」で、誤りではない
        if (!name.empty() && mesh->UsesStandIn())
        {
            NS_LOG_WARN(Scene, "メッシュ {} を読めないので cube で描く", name);
        }
        return mesh;
    }

    const Phys::MeshShape* AssetManager::LoadMeshShape(std::string_view name)
    {
        // 描く物と同じく、空の名前は cube にする
        std::string_view shapeName = name;
        if (shapeName.empty())
        {
            shapeName = "cube";
        }
        if (FindBuiltinShape(shapeName) != nullptr)
        {
            return WithShape(BuiltinMesh(shapeName).Collision());
        }

        const Phys::MeshShape* collision = nullptr;
        if (const std::optional<std::string> path = ResolveContentPath(std::string{name}))
        {
            collision = WithShape(LoadMeshFile(*path, name).Collision());
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
        return LoadSkinnedModelFile(*path, name);
    }

    const SkinnedModel* AssetManager::LoadSkinnedModelFile(std::string_view path, std::string_view name)
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

        Gfx::D3DSkeletalMeshDesc smd{};
        smd.vertices = data.vertices.data();
        smd.vertexCount = data.vertices.size();
        smd.indices = data.indices.data();
        smd.indexCount = data.indices.size();
        smd.boneCount = data.skeleton.BoneCount();

        std::unique_ptr<Gfx::D3DSkeletalMesh> mesh = Gfx::D3DSkeletalMesh::Create(smd);
        if (mesh == nullptr || !mesh->IsValid())
        {
            // GPU buffer 生成に失敗。壊れた mesh をキャッシュせず無効を返し、Draw が無音で何もしないのを防ぐ
            NS_LOG_ERROR(Graphics, "AssetManager: skinned mesh の GPU 生成失敗: {}", key);
            return nullptr;
        }

        SkinnedModel model{};
        model.mesh = std::make_unique<Mesh>(std::string{name}, std::move(mesh));
        model.skeleton = std::move(data.skeleton);
        return &m_skinnedModels.emplace(key, std::move(model)).first->second;
    }

    const Gfx::AnimSource* AssetManager::GetOrLoadAnimationSource(std::string_view path)
    {
        const std::string key = OS::FileSystem::Normalize(path);
        // 空の項目は読み込み失敗の記録。get() が nullptr を返し、再読み込みを避ける
        if (const std::map<std::string, std::unique_ptr<Gfx::AnimSource>>::iterator it =
                m_animationSources.find(key);
            it != m_animationSources.end())
        {
            return it->second.get();
        }

        Gfx::AnimSource source = Gfx::LoadGltfAnimationSource(key);
        if (!source.IsValid())
        {
            NS_LOG_WARN(Graphics, "AssetManager: アニメーション glTF の読込失敗 / 空: {}", key);
            // 壊れた path を負キャッシュし、毎回ディスクを読むのを防ぐ。再試行は Clear() から
            m_animationSources.emplace(key, nullptr);
            return nullptr;
        }

        std::unique_ptr<Gfx::AnimSource> owned = std::make_unique<Gfx::AnimSource>(std::move(source));
        const Gfx::AnimSource* raw = owned.get();
        m_animationSources.emplace(key, std::move(owned));
        return raw;
    }

    const Gfx::AnimClip* AssetManager::LoadAnimClip(std::string_view name)
    {
        if (name.empty())
        {
            return nullptr;
        }
        const std::size_t hash = name.find('#');
        const std::string_view file = name.substr(0, hash);
        const std::optional<std::string> path = ResolveContentPath(std::string{file});
        if (!path)
        {
            NS_LOG_WARN(Scene, "クリップ {} は ContentRoot の外を指すので読まない", name);
            return nullptr;
        }
        const Gfx::AnimSource* source = GetOrLoadAnimationSource(*path);
        if (source == nullptr)
        {
            // 読めなかった理由は GetOrLoadAnimationSource が報告している
            return nullptr;
        }
        if (hash == std::string_view::npos)
        {
            return &source->animations.front();
        }
        const std::string_view clipName = name.substr(hash + 1);
        for (const Gfx::AnimClip& clip : source->animations)
        {
            if (clip.name == clipName)
            {
                return &clip;
            }
        }
        NS_LOG_WARN(Scene, "クリップ {} の動き {} がファイルに無い", name, clipName);
        return nullptr;
    }

    std::span<const std::string_view> AssetManager::BuiltinMeshNames() noexcept
    {
        static constexpr std::array<std::string_view, k_BuiltinShapes.size()> names = [] {
            std::array<std::string_view, k_BuiltinShapes.size()> result{};
            for (std::size_t i = 0; i < k_BuiltinShapes.size(); ++i)
            {
                result[i] = k_BuiltinShapes[i].name;
            }
            return result;
        }();
        return names;
    }

    std::span<const std::string_view> AssetManager::SharedMaterialNames() noexcept
    {
        return k_SharedMaterialNames;
    }

    void AssetManager::RegisterBuiltins()
    {
        for (const BuiltinShape& shape : k_BuiltinShapes)
        {
            const Gfx::MeshGeometry geom = shape.make();
            const std::map<std::string, std::unique_ptr<Mesh>>::iterator it = m_builtins.find(shape.name);
            if (it == m_builtins.end())
            {
                m_builtins.emplace(shape.name,
                                   std::make_unique<Mesh>(shape.name, MakeStaticMesh(geom), MakeCollision(geom)));
                continue;
            }
            if (it->second->D3D() == nullptr)
            {
                it->second->SetD3D(MakeStaticMesh(geom));
            }
        }
    }

    Material* AssetManager::LoadMaterial(std::string_view name)
    {
        Material* material = FindSharedMaterial(name);
        // 共有の名前はファイルでないので、登録の前でもファイルとして読まない
        if (material == nullptr && !name.empty() && !IsSharedMaterialName(name))
        {
            if (const std::optional<std::string> path = ResolveContentPath(std::string{name}))
            {
                material = LoadMaterialFile(*path, name);
            }
        }
        if (material != nullptr)
        {
            return material;
        }
        // 空の名前は「マテリアルを選んでいない」で、誤りではない
        // 共有の名前は装置が無くて登録されていない時だけ外れる
        if (!name.empty() && !IsSharedMaterialName(name))
        {
            NS_LOG_WARN(Scene, "マテリアル {} を読めないので共有の player で描く", name);
        }
        return &MissingMaterial(name);
    }

    Material& AssetManager::MissingMaterial(std::string_view name)
    {
        const std::string key{name};
        std::map<std::string, std::unique_ptr<Material>>::iterator it = m_missingMaterials.find(key);
        if (it == m_missingMaterials.end())
        {
            std::unique_ptr<Material> material = std::make_unique<Material>(key);
            // RegisterSharedMaterials の前は代わりが無く、何も描かない。player は登録の時に差す
            material->SetStandIn(FindSharedMaterial("player"));
            it = m_missingMaterials.emplace(key, std::move(material)).first;
        }
        return *it->second;
    }

    Material* AssetManager::LoadMaterialFile(const std::string& path, std::string_view name)
    {
        const std::string matKey = OS::FileSystem::Normalize(path);
        if (const std::map<std::string, std::unique_ptr<Material>>::iterator it = m_materials.find(matKey);
            it != m_materials.end())
        {
            return it->second.get();
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

        return m_materials.emplace(matKey, CreateMaterial(name, std::move(fileDesc))).first->second.get();
    }

    std::unique_ptr<Material> AssetManager::CreateMaterial(std::string_view name, MaterialFileDesc file)
    {
        // 相対 path は構築時の baseDir 基準で解決する。絶対 / ドライブ相対は Combine が baseDir を捨てて通す
        const auto resolve = [this](std::string_view p) { return OS::FileSystem::Combine(m_baseDir, p); };

        Shader* vertexShader = LoadShader(resolve(file.vertexShader));
        Shader* pixelShader = LoadShader(resolve(file.pixelShader));
        std::vector<Texture*> textures;
        textures.reserve(file.textures.size());
        for (const std::string& texture : file.textures)
        {
            textures.push_back(LoadTexture(resolve(texture)));
        }
        return std::make_unique<Material>(std::string{name}, std::move(file), vertexShader, pixelShader, textures);
    }

    void AssetManager::RegisterSharedMaterials()
    {
        // .mat と同じ値の形で組み、.mat と同じ道でシェーダーとテクスチャを引く
        // water は描く 1 回ごとの不透明度を出す water.ps を使い、混ぜ方は Alpha にする
        // shadow は shadow.ps が放射状の透明度を作るのでテクスチャを貼らない
        const MaterialFileDesc player{.vertexShader = "Shaders/standard.vs.hlsl",
                                      .pixelShader = "Shaders/player.ps.hlsl",
                                      .textures = {"Assets/Textures/cube_test.png"}};
        MaterialFileDesc water = player;
        water.pixelShader = "Shaders/water.ps.hlsl";
        water.blend = Gfx::BlendMode::Alpha;
        MaterialFileDesc shadow = player;
        shadow.pixelShader = "Shaders/shadow.ps.hlsl";
        shadow.textures.clear();
        shadow.blend = Gfx::BlendMode::Alpha;

        const auto add = [this](const char* name, const MaterialFileDesc& file) {
            if (!m_sharedMaterials.contains(name))
            {
                m_sharedMaterials.emplace(name, CreateMaterial(name, file));
            }
        };
        add("player", player);
        add("water", water);
        add("shadow", shadow);

        const Material* standIn = FindSharedMaterial("player");
        for (std::pair<const std::string, std::unique_ptr<Material>>& entry : m_missingMaterials)
        {
            entry.second->SetStandIn(standIn);
        }
    }

    Material* AssetManager::FindSharedMaterial(std::string_view name) const noexcept
    {
        const std::map<std::string, std::unique_ptr<Material>>::const_iterator it =
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
        if (const std::map<std::string, std::unique_ptr<Shader>>::iterator it = m_shaders.find(key);
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
        for (std::pair<const std::string, std::unique_ptr<Shader>>& entry : m_shaders)
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
        m_missingMaterials.clear();
        m_sharedMaterials.clear();
        m_materials.clear();
        m_textures.clear();
        m_shaders.clear();
        m_meshes.clear();
        m_missingMeshes.clear();
        m_skinnedModels.clear();
        m_animationSources.clear();
        m_builtins.clear();
    }
} // namespace NS
