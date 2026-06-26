#include <gtest/gtest.h>

#include <Framework/Core/Filesystem.h>
#include <Framework/Graphics/Mesh.h>
#include <Framework/Graphics/Renderer.h>
#include <Framework/Graphics/StaticMesh.h>
#include <Framework/Platform/Window.h>
#include <Framework/Scene/AssetManager.h>
#include <Framework/Scene/Components/MeshRendererComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Game/Blocks/BlockRegistry.h>
#include <Game/Blocks/BuildPlacedObject.h>
#include <Game/Level/LevelData.h>

#include <filesystem>
#include <string>
#include <vector>

namespace
{
    using NS::Game::Blocks::BuildPlacedObject;
    using NS::Game::Blocks::FindComponent;
    using NS::Game::Blocks::kBlockIdSlope45;
    using NS::Game::Blocks::kBlockIdSolid;
    using NS::Game::Blocks::ResolveContentPath;
    using NS::Game::Blocks::ResolveMeshFromRef;
    using NS::Game::Level::ComponentData;
    using NS::Game::Level::FieldValue;
    using NS::Game::Level::kObjectFlagGridAligned;
    using NS::Game::Level::ObjectInstance;
    using NS::Scene::AssetManager;
    using NS::Scene::MeshRendererComponent;

    // MeshRendererComponent を 1 つ持つ component 表現。 meshRef が非空なら反射 "Mesh" フィールドに載せる
    ComponentData MakeMeshRenderer(const std::string& meshRef)
    {
        ComponentData c;
        c.typeName = "MeshRendererComponent";
        if (!meshRef.empty())
            c.fields.push_back(FieldValue{"Mesh", meshRef});
        return c;
    }

    NS::Platform::WindowDesc MakeWindowDesc(const char* title)
    {
        NS::Platform::WindowDesc d{};
        d.title = title;
        d.size = NS::Math::Size2D{320, 240};
        d.visible = false;
        return d;
    }

    NS::Graphics::RendererDesc MakeRendererDesc()
    {
        NS::Graphics::RendererDesc d{};
        d.vsync = false;
        d.enableDebugLayer = false;
        return d;
    }
} // namespace

// builtin 名は AssetManager::Builtin の先引きへ解決される。 builtin は device 確立後にしか登録できないので headless は
// skip
TEST(MeshRefResolution, BuiltinNameResolvesToBuiltinMesh)
{
    NS::Platform::Window window(MakeWindowDesc("ns_meshref_builtin"));
    ASSERT_TRUE(window.IsValid());
    NS::Graphics::Renderer renderer(MakeRendererDesc(), window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "Device 確立不可 (headless)";

    AssetManager assets{NS::Core::FileSystem::ContentRoot()};
    assets.RegisterBuiltins();

    ASSERT_NE(assets.Builtin("cube"), nullptr);
    EXPECT_EQ(ResolveMeshFromRef(assets, "cube"), assets.Builtin("cube"));
    EXPECT_EQ(ResolveMeshFromRef(assets, "wedge45"), assets.Builtin("wedge45"));
    EXPECT_EQ(ResolveMeshFromRef(assets, "pole"), assets.Builtin("pole"));
}

// メッシュ参照が空の component 駆動 object は従来 kind 経路 (slope45 → wedge45) へフォールバックする
TEST(MeshRefResolution, EmptyMeshRefFallsBackToKindMesh)
{
    AssetManager assets{std::filesystem::path{"."}};
    const std::vector<std::string> noPaths;

    ObjectInstance obj;
    obj.kind = kBlockIdSlope45;
    obj.flags = kObjectFlagGridAligned;
    obj.components.push_back(MakeMeshRenderer(""));

    auto built = BuildPlacedObject(obj, assets, noPaths);
    ASSERT_NE(built, nullptr);
    auto* mr = FindComponent<MeshRendererComponent>(*built);
    ASSERT_NE(mr, nullptr);
    EXPECT_EQ(mr->GetMesh(), assets.Builtin("wedge45"));
}

// ".." で ContentRoot の外へ出る参照は path 解決で弾かれ、 任意ファイル読込にならない
TEST(MeshRefResolution, TraversalRefIsRejected)
{
    EXPECT_FALSE(ResolveContentPath("../secret.gltf").has_value());

    AssetManager assets{std::filesystem::path{"."}};
    EXPECT_EQ(ResolveMeshFromRef(assets, "../secret.gltf"), nullptr);
}

// 非 traversal の相対 path は受理され、 builtin でなく ContentRoot 配下の GetOrLoadMesh 経路へ委譲される
TEST(MeshRefResolution, RelativePathAttemptsContentRootLoad)
{
    const auto resolved = ResolveContentPath("meshes/foo.gltf");
    ASSERT_TRUE(resolved.has_value());

    AssetManager assets{std::filesystem::path{"."}};
    // 実ファイル不在では両者 nullptr。 解決パスの GetOrLoadMesh へ委譲されることを示す
    EXPECT_EQ(ResolveMeshFromRef(assets, "meshes/foo.gltf"), assets.GetOrLoadMesh(*resolved));
}

// メッシュ参照が空の component 駆動 grid solid と、 kind 駆動 grid solid が同一の cube mesh に解決される回帰
TEST(MeshRefResolution, ComponentsDrivenWithoutMeshRefMatchesKindMesh)
{
    AssetManager assets{std::filesystem::path{"."}};
    const std::vector<std::string> noPaths;

    ObjectInstance compObj;
    compObj.kind = kBlockIdSolid;
    compObj.flags = kObjectFlagGridAligned;
    compObj.components.push_back(MakeMeshRenderer(""));

    ObjectInstance kindObj;
    kindObj.kind = kBlockIdSolid;
    kindObj.flags = kObjectFlagGridAligned;
    ASSERT_TRUE(kindObj.components.empty());

    auto compBuilt = BuildPlacedObject(compObj, assets, noPaths);
    auto kindBuilt = BuildPlacedObject(kindObj, assets, noPaths);
    ASSERT_NE(compBuilt, nullptr);
    ASSERT_NE(kindBuilt, nullptr);

    auto* compMesh = FindComponent<MeshRendererComponent>(*compBuilt);
    auto* kindMesh = FindComponent<MeshRendererComponent>(*kindBuilt);
    ASSERT_NE(compMesh, nullptr);
    ASSERT_NE(kindMesh, nullptr);

    EXPECT_EQ(compMesh->GetMesh(), kindMesh->GetMesh());
    EXPECT_EQ(compMesh->GetMesh(), assets.Builtin("cube"));
}
