#include "Framework/Scene/AssetManager.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Graphics/Mesh.h"
#include "Framework/Graphics/MeshPrimitives.h"
#include "Framework/Graphics/Shader.h"
#include "Framework/Graphics/StaticMesh.h"
#include "Framework/Graphics/Texture.h"
#include "Framework/Math/Math.h"

#include <utility>

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
    } // namespace

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

    bool AssetManager::Reload(const std::filesystem::path& path)
    {
        const std::filesystem::path key = path.lexically_normal();
        if (const auto it = m_shaders.find(key); it != m_shaders.end())
            return it->second->Reload();
        NS_LOG_WARN(::NS::Core::LogCat::Graphics, "AssetManager::Reload: 未キャッシュの path: {}", key.string());
        return false;
    }

    void AssetManager::Clear() noexcept
    {
        m_textures.clear();
        m_shaders.clear();
        m_meshes.clear();
        m_builtins.clear();
    }
} // namespace NS::Scene
