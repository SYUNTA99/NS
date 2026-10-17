#include "NSlib/CoreObject/Asset/AssetManager.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Geometry/MeshPrimitives.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Asset/AnimClip.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/Asset/Shader.h"
#include "NSlib/CoreObject/Asset/SkinnedModel.h"
#include "NSlib/CoreObject/Asset/Texture.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/Gfx/D3DMaterial.h"
#include "NSlib/Gfx/D3DMesh.h"
#include "NSlib/Gfx/D3DShader.h"
#include "NSlib/Gfx/D3DSkeletalMesh.h"
#include "NSlib/Gfx/D3DStaticMesh.h"
#include "NSlib/Gfx/D3DTexture.h"
#include "NSlib/Gfx/Renderer.h"
#include "NSlib/Phys/MeshShape.h"
#include "NSlib/Windows/Filesystem.h"

#include "NSlib/Core/Types.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
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
            MeshGeometry (*make)();
        };

        constexpr std::array<BuiltinShape, 8> k_BuiltinShapes = {{
            {"cube", [] { return MakeCube(Vector3{0.5f, 0.5f, 0.5f}); }},
            {"sphere", [] { return MakeSphere(0.5f); }},
            {"capsule", [] { return MakeCapsule(0.5f, 0.5f); }},
            {"wedge45", [] { return MakeSlope(45.0f, Vector3{0.5f, 0.5f, 0.5f}); }},
            {"wedge30", [] { return MakeSlope(30.0f, Vector3{0.5f, 0.5f, 0.5f}); }},
            {"wedge22", [] { return MakeSlope(22.5f, Vector3{0.5f, 0.5f, 0.5f}); }},
            {"wedge15", [] { return MakeSlope(15.0f, Vector3{0.5f, 0.5f, 0.5f}); }},
            {"shadowQuad", [] { return MakePlane(Vector2{0.5f, 0.5f}); }},
        }};

        // 組み込みの形と空のメッシュを描く灰色の既定と、読めない物を描くピンク
        constexpr std::string_view k_DefaultMaterialPath = "Assets/Materials/default.mat";
        constexpr std::string_view k_MissingMaterialPath = "Assets/Materials/missing.mat";

        // 欄の名前を表の鍵にする。区切り文字や . / .. の表記揺れを 1 つの鍵にまとめる
        [[nodiscard]] std::string NameKey(std::string_view name)
        {
            if (name.empty())
            {
                return {};
            }
            return OS::FileSystem::Normalize(name);
        }

        // create は同じ表へ別の鍵を足してよい
        template <class T, class Create>
        [[nodiscard]] T& FindOrAdd(std::map<std::string, std::unique_ptr<T>>& table, const std::string& key, Create create)
        {
            typename std::map<std::string, std::unique_ptr<T>>::iterator it = table.find(key);
            if (it == table.end())
            {
                std::unique_ptr<T> created = create();
                it = table.emplace(key, std::move(created)).first;
            }
            return *it->second;
        }

        // RegisterBuiltins を呼んでいなくても引ける
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
        [[nodiscard]] std::vector<Phys::Triangle> MakeTriangles(const MeshGeometry& geom)
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
        [[nodiscard]] std::unique_ptr<Gfx::D3DStaticMesh> MakeStaticMesh(const MeshGeometry& geom)
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

        [[nodiscard]] std::unique_ptr<Phys::MeshShape> MakeCollision(const MeshGeometry& geom)
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

    namespace
    {
        // 読み直すと同じ float になる一番短い綴り。整数の値は今の .mat と同じく .0 を付ける
        [[nodiscard]] std::string FloatJson(float value)
        {
            std::string text = std::format("{}", value);
            if (text.find_first_of(".e") == std::string::npos)
            {
                text += ".0";
            }
            return text;
        }

        // nlohmann は UTF-8 でない文字列で例外を投げるので、置き換え文字で書く
        [[nodiscard]] std::string StringJson(std::string_view value)
        {
            return nlohmann::json(value).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }
    } // namespace

    std::optional<std::string> MakeMaterialJson(const MaterialFileDesc& file)
    {
        if (file.textures.size() > k_MaterialTextureSlots)
        {
            return std::nullopt;
        }
        const Vector3& color = file.baseColor;
        if (!std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z))
        {
            return std::nullopt;
        }

        std::string text = "{\n";
        text += "  \"vs\": " + StringJson(file.vertexShader) + ",\n";
        text += "  \"ps\": " + StringJson(file.pixelShader) + ",\n";
        if (!file.textures.empty())
        {
            text += "  \"textures\": [";
            for (std::size_t i = 0; i < file.textures.size(); ++i)
            {
                if (i > 0)
                {
                    text += ", ";
                }
                text += StringJson(file.textures[i]);
            }
            text += "],\n";
        }
        text += std::format(
            "  \"baseColor\": [{}, {}, {}],\n", FloatJson(color.x), FloatJson(color.y), FloatJson(color.z));
        text += "  \"blend\": " + StringJson(BlendName(file.blend)) + "\n";
        text += "}\n";
        return text;
    }

    namespace
    {
        template <class Result>
        [[nodiscard]] Result LoadGltfFile(std::string_view path, Result (*parse)(const GltfFileDesc&))
        {
            const std::optional<std::vector<std::byte>> bytes = OS::FileSystem::ReadAllBytes(path);
            if (!bytes)
            {
                return Result{};
            }
            const std::string directory = OS::FileSystem::ParentDirectory(path);
            GltfFileDesc file{};
            file.bytes = *bytes;
            file.name = path;
            file.baseDirectory = directory;
            file.readFile = [](std::string_view bufferPath) { return OS::FileSystem::ReadAllBytes(bufferPath); };
            return parse(file);
        }
    } // namespace

    MeshGeometry LoadGltfMesh(std::string_view path)
    {
        return LoadGltfFile(path, &ParseGltfMesh);
    }

    SkinnedMeshData LoadGltfSkinnedMesh(std::string_view path)
    {
        return LoadGltfFile(path, &ParseGltfSkinnedMesh);
    }

    AnimSource LoadGltfAnimationSource(std::string_view path)
    {
        return LoadGltfFile(path, &ParseGltfAnimationSource);
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
        // 差していない間に返す。RegisterBuiltins は呼んでいない
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

    Gfx::BuiltinShaders AssetManager::LoadBuiltinShaders()
    {
        const std::string directory = OS::FileSystem::Combine(m_baseDir, "Shaders");
        const auto load = [this, &directory](std::string_view fileName) {
            return LoadShader(OS::FileSystem::Combine(directory, fileName))->D3D();
        };
        Gfx::BuiltinShaders shaders{};
        shaders.fullscreenVs = load("fade.vs.hlsl");
        shaders.fadePs = load("fade.ps.hlsl");
        shaders.screenRectVs = load("ui_rect.vs.hlsl");
        shaders.screenRectPs = load("ui_rect.ps.hlsl");
        shaders.skyboxVs = load("skybox.vs.hlsl");
        shaders.skyboxPs = load("skybox.ps.hlsl");
        shaders.debugLineVs = load("debug_line.vs.hlsl");
        shaders.debugLinePs = load("debug_line.ps.hlsl");
        shaders.bloomThresholdPs = load("bloom_threshold.ps.hlsl");
        shaders.bloomDownPs = load("bloom_down.ps.hlsl");
        shaders.bloomUpPs = load("bloom_up.ps.hlsl");
        shaders.bloomCompositePs = load("bloom_composite.ps.hlsl");
        shaders.screenDistortionPs = load("screen_distortion.ps.hlsl");
        shaders.bodyMaskPs = load("body_mask.ps.hlsl");
        return shaders;
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

    std::unique_ptr<Mesh> AssetManager::CreateMesh(const std::string& key)
    {
        // 空の名前は「メッシュを選んでいない」で、誤りではない
        if (key.empty())
        {
            return std::make_unique<Mesh>(key, nullptr, nullptr);
        }
        // 組み込みの形の名前はファイルでないので、登録の前でもファイルとして読まない
        if (const BuiltinShape* shape = FindBuiltinShape(key))
        {
            return std::make_unique<Mesh>(key, nullptr, MakeCollision(shape->make()));
        }
        const std::optional<std::string> path = ResolveContentPath(key);
        if (!path)
        {
            NS_LOG_ERROR(Scene, "メッシュ {} は ContentRoot の外を指すので読まず、何も描かない", key);
            return std::make_unique<Mesh>(key, nullptr, nullptr);
        }

        // 描画と当たりのどちらを先に頼まれても両方ここで作る
        const MeshGeometry geom = LoadGltfMesh(*path);
        if (geom.vertices.empty() || geom.indices.empty())
        {
            NS_LOG_ERROR(Scene, "メッシュ {} を読めないので、何も描かず当たりも作らない", key);
            return std::make_unique<Mesh>(key, nullptr, nullptr);
        }
        std::unique_ptr<Gfx::D3DMesh> drawn;
        std::unique_ptr<Gfx::D3DStaticMesh> mesh = MakeStaticMesh(geom);
        if (mesh == nullptr || !mesh->IsValid())
        {
            NS_LOG_ERROR(Graphics, "AssetManager: mesh の GPU 生成失敗。 描かず、 当たりは本物の形のまま: {}", key);
        }
        else
        {
            drawn = std::move(mesh);
        }
        // GPU 生成だけ失敗しても当たりは作る。device 無しのテストでも当たりを確かめられる
        // 当たりだけ頼まれた時も GPU 生成を通るので、device の無い所でファイルの mesh を読むと上のエラーが出る
        // TODO: コライダーの無い描画だけの mesh も三角形を Clear() まで持つ
        // 大きな mesh を飾りに多く置いてメモリが効いてきたら、当たりを頼まれた時に作る形へ移す
        return std::make_unique<Mesh>(key, std::move(drawn), MakeCollision(geom));
    }

    Mesh* AssetManager::LoadMesh(std::string_view name)
    {
        const std::string key = NameKey(name);
        return &FindOrAdd(m_meshes, key, [this, &key] { return CreateMesh(key); });
    }

    const Phys::MeshShape* AssetManager::LoadMeshShape(std::string_view name)
    {
        return WithShape(LoadMesh(name)->Collision());
    }

    SkinnedModel* AssetManager::LoadSkinnedModel(std::string_view name)
    {
        const std::string key = NameKey(name);
        return &FindOrAdd(m_skinnedModels, key, [this, &key] { return CreateSkinnedModel(key); });
    }

    std::unique_ptr<SkinnedModel> AssetManager::CreateSkinnedModel(const std::string& key)
    {
        if (key.empty())
        {
            return std::make_unique<SkinnedModel>(key);
        }
        const std::optional<std::string> path = ResolveContentPath(key);
        if (!path)
        {
            NS_LOG_ERROR(Scene, "骨付きのモデル {} は ContentRoot の外を指すので読まない", key);
            return std::make_unique<SkinnedModel>(key);
        }

        SkinnedMeshData data = LoadGltfSkinnedMesh(*path);
        if (!data.IsValid())
        {
            NS_LOG_ERROR(Graphics, "AssetManager: skinned glTF 読込失敗: {}", key);
            return std::make_unique<SkinnedModel>(key);
        }

        if (data.vertices.empty())
        {
            NS_LOG_ERROR(Graphics, "AssetManager: skinned mesh に頂点が無い: {}", key);
            return std::make_unique<SkinnedModel>(key);
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
            NS_LOG_ERROR(Graphics, "AssetManager: skinned mesh の GPU 生成失敗: {}", key);
            return std::make_unique<SkinnedModel>(key);
        }

        return std::make_unique<SkinnedModel>(key, std::make_unique<Mesh>(key, std::move(mesh)), std::move(data.skeleton));
    }

    const AnimSource* AssetManager::GetOrLoadAnimationSource(std::string_view path)
    {
        const std::string key = OS::FileSystem::Normalize(path);
        // 空の項目は読み込み失敗の記録。get() が nullptr を返し、再読み込みを避ける
        if (const std::map<std::string, std::unique_ptr<AnimSource>>::iterator it =
                m_animationSources.find(key);
            it != m_animationSources.end())
        {
            return it->second.get();
        }

        AnimSource source = LoadGltfAnimationSource(key);
        if (!source.IsValid())
        {
            NS_LOG_WARN(Graphics, "AssetManager: アニメーション glTF の読込失敗 / 空: {}", key);
            // 壊れた path を負キャッシュし、毎回ディスクを読むのを防ぐ。再試行は Clear() から
            m_animationSources.emplace(key, nullptr);
            return nullptr;
        }

        std::unique_ptr<AnimSource> owned = std::make_unique<AnimSource>(std::move(source));
        const AnimSource* raw = owned.get();
        m_animationSources.emplace(key, std::move(owned));
        return raw;
    }

    AnimClip* AssetManager::LoadAnimClip(std::string_view name)
    {
        if (const std::map<std::string, std::unique_ptr<AnimClip>, std::less<>>::iterator it = m_animClips.find(name);
            it != m_animClips.end())
        {
            return it->second.get();
        }
        return m_animClips.emplace(std::string{name}, CreateAnimClip(name)).first->second.get();
    }

    std::unique_ptr<AnimClip> AssetManager::CreateAnimClip(std::string_view name)
    {
        if (name.empty())
        {
            return std::make_unique<AnimClip>(std::string{}, nullptr);
        }
        const std::size_t hash = name.find('#');
        const std::string_view file = name.substr(0, hash);
        const std::optional<std::string> path = ResolveContentPath(std::string{file});
        if (!path)
        {
            NS_LOG_WARN(Scene, "クリップ {} は ContentRoot の外を指すので読まない", name);
            return std::make_unique<AnimClip>(std::string{name}, nullptr);
        }
        const AnimSource* source = GetOrLoadAnimationSource(*path);
        if (source == nullptr)
        {
            // 読めなかった理由は GetOrLoadAnimationSource が報告している
            return std::make_unique<AnimClip>(std::string{name}, nullptr);
        }
        if (hash == std::string_view::npos)
        {
            return std::make_unique<AnimClip>(std::string{name},
                                              std::make_unique<Anim::AnimClip>(source->animations.front()));
        }
        const std::string_view clipName = name.substr(hash + 1);
        for (const Anim::AnimClip& clip : source->animations)
        {
            if (clip.name == clipName)
            {
                return std::make_unique<AnimClip>(std::string{name}, std::make_unique<Anim::AnimClip>(clip));
            }
        }
        NS_LOG_WARN(Scene, "クリップ {} の動き {} がファイルに無い", name, clipName);
        return std::make_unique<AnimClip>(std::string{name}, nullptr);
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

    void AssetManager::RegisterBuiltins()
    {
        for (const BuiltinShape& shape : k_BuiltinShapes)
        {
            const MeshGeometry geom = shape.make();
            Mesh& mesh = FindOrAdd(m_meshes, std::string{shape.name}, [&shape, &geom] {
                return std::make_unique<Mesh>(std::string{shape.name}, nullptr, MakeCollision(geom));
            });
            if (mesh.D3D() == nullptr)
            {
                mesh.SetD3D(MakeStaticMesh(geom));
            }
        }
    }

    Material* AssetManager::LoadMaterial(std::string_view name)
    {
        const std::string key = NameKey(name);
        return &FindOrAdd(m_materials, key, [this, &key] { return ReadMaterial(key); });
    }

    Material* AssetManager::LoadUnsetMaterial(const Mesh& mesh)
    {
        // 空のメッシュは何も描かないので、ERROR を出さず灰色にする。読めない時の ERROR はメッシュが出している
        if (mesh.IsEmpty() || FindBuiltinShape(mesh.Path()) != nullptr)
        {
            return LoadMaterial(k_DefaultMaterialPath);
        }
        NS_LOG_ERROR(Scene, "メッシュ {} にマテリアルが無いので、ピンクで描く", mesh.Path());
        return LoadMaterial(k_MissingMaterialPath);
    }

    std::unique_ptr<Material> AssetManager::ReadMaterial(const std::string& key)
    {
        // 空の名前は「マテリアルを選んでいない」で、誤りではない
        if (key.empty())
        {
            return std::make_unique<Material>(key);
        }
        const std::optional<std::string> path = ResolveContentPath(key);
        if (!path)
        {
            NS_LOG_ERROR(Scene, "マテリアル {} は ContentRoot の外を指すので読まず、ピンクで描く", key);
            return MakeUnreadableMaterial(key);
        }
        const std::optional<std::string> text = OS::FileSystem::ReadAllText(*path);
        if (!text)
        {
            NS_LOG_ERROR(Scene, "マテリアル {} を読めないので、ピンクで描く", key);
            return MakeUnreadableMaterial(key);
        }
        MaterialFileDesc fileDesc{};
        std::string parseError;
        if (!ParseMaterialJson(*text, fileDesc, parseError))
        {
            NS_LOG_ERROR(Scene, "マテリアル {} の中身が壊れている ({}) ので、ピンクで描く", key, parseError);
            return MakeUnreadableMaterial(key);
        }
        return CreateMaterial(key, std::move(fileDesc));
    }

    std::unique_ptr<Material> AssetManager::MakeUnreadableMaterial(const std::string& key)
    {
        std::unique_ptr<Material> material = std::make_unique<Material>(key);
        if (key != k_MissingMaterialPath)
        {
            material->SetStandIn(LoadMaterial(k_MissingMaterialPath));
        }
        return material;
    }

    std::unique_ptr<Material> AssetManager::CreateMaterial(std::string_view name, MaterialFileDesc file)
    {
        const MaterialSources sources = LoadMaterialSources(file);
        return std::make_unique<Material>(
            std::string{name}, std::move(file), sources.vertexShader, sources.pixelShader, sources.textures);
    }

    AssetManager::MaterialSources AssetManager::LoadMaterialSources(const MaterialFileDesc& file)
    {
        // 相対 path は構築時の baseDir 基準で解決する。絶対 / ドライブ相対は Combine が baseDir を捨てて通す
        const auto resolve = [this](std::string_view p) { return OS::FileSystem::Combine(m_baseDir, p); };

        MaterialSources sources{};
        sources.vertexShader = LoadShader(resolve(file.vertexShader));
        sources.pixelShader = LoadShader(resolve(file.pixelShader));
        sources.textures.reserve(file.textures.size());
        for (const std::string& texture : file.textures)
        {
            sources.textures.push_back(LoadTexture(resolve(texture)));
        }
        return sources;
    }

    std::optional<std::string> AssetManager::MakeMaterialFileJson(const Material& material) const
    {
        if (material.IsEmpty())
        {
            return std::nullopt;
        }
        return MakeMaterialJson(material.File());
    }

    bool AssetManager::RebuildMaterial(Material& material)
    {
        if (material.IsEmpty())
        {
            return false;
        }
        const MaterialSources sources = LoadMaterialSources(material.File());
        return material.Rebuild(sources.vertexShader, sources.pixelShader, sources.textures);
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
        m_materials.clear();
        m_textures.clear();
        m_shaders.clear();
        m_meshes.clear();
        m_skinnedModels.clear();
        m_animClips.clear();
        m_animationSources.clear();
    }
} // namespace NS
