#include "NSlib/CoreObject/Asset/AssetManager.h"

#include "NSlib/Audio/XA2Sound.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Geometry/MeshPrimitives.h"
#include "NSlib/Core/Geometry/SkinnedBounds.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/CoreObject/Asset/AnimClip.h"
#include "NSlib/CoreObject/Asset/Effect.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/MaterialFile.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/Asset/Shader.h"
#include "NSlib/CoreObject/Asset/Sound.h"
#include "NSlib/CoreObject/Asset/Texture.h"
#include "NSlib/Gfx/D3DEffect.h"
#include "NSlib/Gfx/D3DMaterial.h"
#include "NSlib/Gfx/D3DMesh.h"
#include "NSlib/Gfx/D3DShader.h"
#include "NSlib/Gfx/D3DSkeletalMesh.h"
#include "NSlib/Gfx/D3DStaticMesh.h"
#include "NSlib/Gfx/D3DTexture.h"
#include "NSlib/Gfx/Render/BuiltinShaders.h"
#include "NSlib/Phys/MeshShape.h"

#include "NSlib/Core/Types.h"
#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace NS
{
    std::optional<std::string> ResolveContentPath(const std::string& relative)
    {
        // ディレクトリトラバーサルの防止は ResolveUnder が持つ
        return OS::FileSystem::ResolveUnder(OS::FileSystem::ContentRoot(), relative);
    }

    namespace
    {
        // 組み込みの形の頂点を作る表。ここから先はファイルのメッシュと同じ道で、描く形と当たりを作る
        struct BuiltinShape
        {
            const char* name;
            std::optional<float> slopeAngleDegrees; // 坂だけが持つ。頂点もこの角度で作る
            MeshGeometry (*make)(const BuiltinShape& shape);
        };

        [[nodiscard]] MeshGeometry MakeBuiltinSlope(const BuiltinShape& shape)
        {
            return MakeSlope(*shape.slopeAngleDegrees, Vector3{0.5f, 0.5f, 0.5f});
        }

        constexpr std::array<BuiltinShape, 8> k_BuiltinShapes = {{
            {"cube", std::nullopt, [](const BuiltinShape&) { return MakeCube(Vector3{0.5f, 0.5f, 0.5f}); }},
            {"sphere", std::nullopt, [](const BuiltinShape&) { return MakeSphere(0.5f); }},
            {"capsule", std::nullopt, [](const BuiltinShape&) { return MakeCapsule(0.5f, 0.5f); }},
            {"wedge45", 45.0f, MakeBuiltinSlope},
            {"wedge30", 30.0f, MakeBuiltinSlope},
            {"wedge22", 22.5f, MakeBuiltinSlope},
            {"wedge15", 15.0f, MakeBuiltinSlope},
            {"shadowQuad", std::nullopt, [](const BuiltinShape&) { return MakePlane(Vector2{0.5f, 0.5f}); }},
        }};

        // 読めない物を描くピンク
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

        // クリップの名前は「ファイル#動きの名前」。揃えるのはファイルの部分だけで、動きの名前は glTF の字面のまま
        [[nodiscard]] std::string ClipKey(std::string_view name)
        {
            const std::size_t hash = name.find('#');
            if (hash == std::string_view::npos)
            {
                return NameKey(name);
            }
            return NameKey(name.substr(0, hash)) + std::string{name.substr(hash)};
        }

        // create は同じ表へ別の鍵を足してよい
        template <class T, class Create>
        [[nodiscard]] T& FindOrAdd(std::map<std::string, std::unique_ptr<T>>& table,
                                   const std::string& key,
                                   Create create)
        {
            typename std::map<std::string, std::unique_ptr<T>>::iterator it = table.find(key);
            if (it == table.end())
            {
                std::unique_ptr<T> created = create();
                it = table.emplace(key, std::move(created)).first;
            }
            return *it->second;
        }

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

                triangles.push_back(
                    Phys::Triangle{geom.vertices[a].position, geom.vertices[b].position, geom.vertices[c].position});
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

        // D3DDevice の無い場では作らずに黙る。GPU で作れなかった時だけ ERROR を出す
        [[nodiscard]] std::unique_ptr<Gfx::D3DMesh> MakeDrawnMesh(const std::string& key, const MeshGeometry& geom)
        {
            std::unique_ptr<Gfx::D3DStaticMesh> mesh = MakeStaticMesh(geom);
            if (mesh == nullptr)
            {
                return nullptr;
            }
            if (!mesh->IsValid())
            {
                NS_LOG_ERROR(Graphics, "AssetManager: mesh の GPU 生成失敗。 描かず、 当たりは本物の形のまま: {}", key);
                return nullptr;
            }
            return mesh;
        }

        [[nodiscard]] std::unique_ptr<Phys::MeshShape> MakeCollision(const MeshGeometry& geom)
        {
            std::unique_ptr<Phys::MeshShape> collision = std::make_unique<Phys::MeshShape>();
            collision->triangles = MakeTriangles(geom);
            return collision;
        }

        // 道の glTF の本体を 1 回だけ読み、use に渡す。本体を読めない時は use を呼ばずに空の値を返す
        // 外部のバッファは本体と同じフォルダから読む
        template <class Fn>
        [[nodiscard]] std::invoke_result_t<Fn, const GltfFileDesc&> WithGltfFile(std::string_view path, Fn&& use)
        {
            const std::optional<std::vector<std::byte>> bytes = OS::FileSystem::ReadAllBytes(path);
            if (!bytes)
            {
                return std::invoke_result_t<Fn, const GltfFileDesc&>{};
            }
            const std::string directory = OS::FileSystem::ParentDirectory(path);
            GltfFileDesc file{};
            file.bytes = *bytes;
            file.name = path;
            file.baseDirectory = directory;
            file.readFile = [](std::string_view bufferPath) { return OS::FileSystem::ReadAllBytes(bufferPath); };
            return use(file);
        }

        // 骨付きの Mesh を作る。骨格の無いファイルは nullptr。描く形を GPU で作れなくても骨格は持つ
        [[nodiscard]] std::unique_ptr<Mesh> ReadSkinnedMesh(const std::string& key,
                                                            const GltfFileDesc& file)
        {
            std::pair<SkinnedMeshGeometry, Anim::Skeleton> skinned = ParseGltfSkinnedMesh(file);
            const SkinnedMeshGeometry& geom = skinned.first;
            Anim::Skeleton& skeleton = skinned.second;
            if (geom.vertices.empty() || geom.indices.empty() || skeleton.BoneCount() == 0u)
            {
                return nullptr;
            }

            Gfx::D3DSkeletalMeshDesc desc{};
            desc.vertices = geom.vertices.data();
            desc.vertexCount = geom.vertices.size();
            desc.indices = geom.indices.data();
            desc.indexCount = geom.indices.size();
            std::unique_ptr<Gfx::D3DSkeletalMesh> mesh = Gfx::D3DSkeletalMesh::Create(desc);
            if (mesh != nullptr && !mesh->IsValid())
            {
                NS_LOG_ERROR(Graphics, "AssetManager: skinned mesh の GPU 生成失敗。 描かず、 骨格は持つ: {}", key);
                mesh = nullptr;
            }

            std::vector<BoneSphere> spheres =
                ComputeBoneSpheres(geom.vertices.data(), geom.vertices.size(), skeleton.BoneCount());
            return std::make_unique<Mesh>(key, std::move(mesh), std::move(skeleton), std::move(spheres));
        }

        // key の glTF を読む。スキン付きのメッシュの node があれば骨付きの Mesh、無ければ描く形と当たりの Mesh
        // 読めない時は ERROR を出して nullptr
        [[nodiscard]] std::unique_ptr<Mesh> ReadMeshFile(const std::string& key)
        {
            const std::optional<std::string> path = ResolveContentPath(key);
            if (!path)
            {
                NS_LOG_ERROR(Scene, "メッシュ {} は ContentRoot の外を指すので読まず、何も描かない", key);
                return nullptr;
            }
            std::unique_ptr<Mesh> mesh =
                WithGltfFile(*path, [&key](const GltfFileDesc& file) -> std::unique_ptr<Mesh> {
                    if (HasGltfSkin(file))
                    {
                        return ReadSkinnedMesh(key, file);
                    }
                    const MeshGeometry geom = ParseGltfMesh(file);
                    if (geom.vertices.empty() || geom.indices.empty())
                    {
                        return nullptr;
                    }
                    // 描く形と当たりのどちらを先に頼まれても両方ここで作る。GPU で作れなくても当たりは作る
                    // TODO: コライダーの無い描画だけの mesh も三角形を Clear() まで持つ
                    // 大きな mesh を飾りに多く置いてメモリが効いてきたら、当たりを頼まれた時に作る形へ移す
                    return std::make_unique<Mesh>(key, MakeDrawnMesh(key, geom), MakeCollision(geom));
                });
            if (mesh == nullptr)
            {
                NS_LOG_ERROR(Scene, "メッシュ {} を読めないので、何も描かず当たりも作らない", key);
            }
            return mesh;
        }
    } // namespace

    MeshGeometry LoadGltfMesh(std::string_view path)
    {
        return WithGltfFile(path, &ParseGltfMesh);
    }

    AnimSource LoadGltfAnimationSource(std::string_view path)
    {
        return WithGltfFile(path, &ParseGltfAnimationSource);
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
        // 差していない間に返す
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
        return &FindOrAdd(m_shaders, key, [&key] {
            std::unique_ptr<Shader> shader = std::make_unique<Shader>(key);
            if (shader->D3D()->IsUsingFallback())
            {
                NS_LOG_ERROR(Graphics, "AssetManager: shader の読込/コンパイル失敗、 fallback 描画: {}", key);
            }
            return shader;
        });
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
        shaders.screenLineVs = load("ui_line.vs.hlsl");
        shaders.screenLinePs = load("ui_line.ps.hlsl");
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
        return &FindOrAdd(m_textures, key, [&key] { return std::make_unique<Texture>(key); });
    }

    std::unique_ptr<Mesh> AssetManager::CreateMesh(const std::string& key)
    {
        // 空の名前は「メッシュを選んでいない」で、誤りではない
        if (key.empty())
        {
            return std::make_unique<Mesh>(key, nullptr, nullptr);
        }

        // 組み込みとファイルで違うのは頂点の出どころ。スキンのあるファイルだけは当たりの代わりに骨格を持つ
        if (const BuiltinShape* shape = FindBuiltinShape(key))
        {
            const MeshGeometry geom = shape->make(*shape);
            return std::make_unique<Mesh>(key, MakeDrawnMesh(key, geom), MakeCollision(geom));
        }
        // 読めない名前の ERROR は ReadMeshFile が出す
        std::unique_ptr<Mesh> file = ReadMeshFile(key);
        if (file == nullptr)
        {
            return std::make_unique<Mesh>(key, nullptr, nullptr);
        }
        return file;
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

    const AnimSource* AssetManager::GetOrLoadAnimationSource(std::string_view path)
    {
        const std::string key = OS::FileSystem::Normalize(path);
        // 空の項目は読み込み失敗の記録。get() が nullptr を返し、再読み込みを避ける
        if (const std::map<std::string, std::unique_ptr<AnimSource>>::iterator it = m_animationSources.find(key);
            it != m_animationSources.end())
        {
            return it->second.get();
        }

        AnimSource source = LoadGltfAnimationSource(key);
        if (!source.IsValid())
        {
            NS_LOG_ERROR(Scene, "アニメーション {} から動きを取り出せないので、中身の空のクリップになる", key);
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
        const std::string key = ClipKey(name);
        return &FindOrAdd(m_animClips, key, [this, &key] { return CreateAnimClip(key); });
    }

    std::unique_ptr<AnimClip> AssetManager::CreateAnimClip(const std::string& key)
    {
        const std::string_view name = key;
        if (name.empty())
        {
            return std::make_unique<AnimClip>(std::string{}, nullptr);
        }
        const std::size_t hash = name.find('#');
        const std::string_view file = name.substr(0, hash);
        const std::optional<std::string> path = ResolveContentPath(std::string{file});
        if (!path)
        {
            NS_LOG_ERROR(Scene, "クリップ {} は ContentRoot の外を指すので読まない", name);
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
        NS_LOG_ERROR(Scene, "クリップ {} の動き {} がファイルに無い", name, clipName);
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

    std::optional<float> AssetManager::BuiltinSlopeAngleDegrees(std::string_view name) noexcept
    {
        const BuiltinShape* shape = FindBuiltinShape(name);
        if (shape == nullptr)
        {
            return std::nullopt;
        }
        return shape->slopeAngleDegrees;
    }

    std::string_view AssetManager::DefaultMaterialPath() noexcept
    {
        return "Assets/Materials/default.mat";
    }

    Material* AssetManager::LoadMaterial(std::string_view name)
    {
        const std::string key = NameKey(name);
        return &FindOrAdd(m_materials, key, [this, &key] { return ReadMaterial(key); });
    }

    std::unique_ptr<Material> AssetManager::ReadMaterial(const std::string& key)
    {
        // 空の名前は「マテリアルを選んでいない」で、誤りではない。中身は空のまま、既定の灰色を黙って描く
        // 読めない名前がピンクを指すのと同じ形にして、欄の持ち主が空かを見分けずに済むようにする
        if (key.empty())
        {
            std::unique_ptr<Material> material = std::make_unique<Material>(key);
            material->SetStandIn(LoadMaterial(DefaultMaterialPath()));
            return material;
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
        MaterialSources sources{};
        sources.vertexShader = LoadShader(OS::FileSystem::Combine(m_baseDir, file.vertexShader));
        sources.pixelShader = LoadShader(OS::FileSystem::Combine(m_baseDir, file.pixelShader));
        sources.textures.reserve(file.textures.size());
        for (const std::string& texture : file.textures)
        {
            sources.textures.push_back(LoadTexture(OS::FileSystem::Combine(m_baseDir, texture)));
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
            if (!it->second->Reload())
            {
                return false;
            }
            RebuildAllMaterials();
            return true;
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
        RebuildAllMaterials();
        return reloaded;
    }

    void AssetManager::RebuildAllMaterials()
    {
        // 作り直せないマテリアルは RebuildMaterial が WARN を出し、前の D3DMaterial で描き続ける
        for (std::pair<const std::string, std::unique_ptr<Material>>& entry : m_materials)
        {
            (void)RebuildMaterial(*entry.second);
        }
    }

    void AssetManager::SetAudioDevice(Audio::AudioDevice* device) noexcept
    {
        // 前に読んだ音は前の装置で作ったか、音を作っていないので、読む前にだけ渡す
        NS_ASSERT(Audio, m_soundClips.empty(), "AssetManager::SetAudioDevice: 音を読んだ後に装置を替えた");
        m_audioDevice = device;
    }

    Sound* AssetManager::LoadSound(std::string_view path)
    {
        const std::string key = NameKey(path);
        return &FindOrAdd(m_soundClips, key, [this, &key]() -> std::unique_ptr<Sound> {
            if (m_audioDevice == nullptr || key.empty())
            {
                return std::make_unique<Sound>(key, nullptr);
            }
            const std::optional<std::string> file = ResolveContentPath(key);
            if (!file)
            {
                NS_LOG_ERROR(Audio, "音 {} は ContentRoot の外を指すので読まない", key);
                return std::make_unique<Sound>(key, nullptr);
            }
            // 読めない理由は Audio::XA2Sound が ERROR で出す
            return std::make_unique<Sound>(key, std::make_unique<Audio::XA2Sound>(m_audioDevice, *file));
        });
    }

    Effect* AssetManager::LoadEffect(std::string_view path)
    {
        const std::string key = NameKey(path);
        return &FindOrAdd(m_effects, key, [this, &key]() -> std::unique_ptr<Effect> {
            if (key.empty())
            {
                return std::make_unique<Effect>(key, nullptr);
            }
            const std::optional<std::string> file = ResolveContentPath(key);
            if (!file)
            {
                NS_LOG_ERROR(Scene, "エフェクト {} は ContentRoot の外を指すので読まない", key);
                return std::make_unique<Effect>(key, nullptr);
            }
            return std::make_unique<Effect>(key, Gfx::D3DEffect::LoadFromFile(*file));
        });
    }

    void AssetManager::Clear() noexcept
    {
        m_effects.clear();
        // 音は装置の声を持つので、装置を壊す Application より先にここで捨てる
        m_soundClips.clear();
        // material は shader / texture を参照するので先に解放する
        m_materials.clear();
        m_textures.clear();
        m_shaders.clear();
        m_meshes.clear();
        m_animClips.clear();
        m_animationSources.clear();
    }
} // namespace NS
