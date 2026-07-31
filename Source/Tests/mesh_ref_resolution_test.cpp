#include <Runtime/Core/Filesystem.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Graphics/StaticMesh.h>
#include <Runtime/Object/AssetManager.h>
#include <Runtime/Object/Components/MeshRendererComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Reflection/ObjectBuilder.h>
#include <Runtime/Object/Scene/SceneData.h>
#include <Runtime/Platform/Window.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>

namespace
{
    using NS::Object::AssetManager;
    using NS::Object::BuildSceneObject;
    using NS::Object::MeshRendererComponent;
    using NS::Object::ObjectData;
    using NS::Object::ResolveContentPath;
    using NS::Object::ResolveMeshFromRef;

    // MeshRendererComponent 1 件分を作る。meshRef が空でなければ "Mesh" フィールドに入れる
    nlohmann::json MakeMeshRenderer(const std::string& meshRef)
    {
        nlohmann::json c = NS::Object::MakeComponentEntry("MeshRendererComponent");
        if (!meshRef.empty())
            NS::Object::SetField(c, "Mesh", meshRef);
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

// builtin 名は AssetManager::Builtin のメッシュへ解決される
// builtin 登録には device が要るので headless では飛ばす
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
}

// メッシュ参照が空なら cube へフォールバックする
TEST(MeshRefResolution, EmptyMeshRefFallsBackToCube)
{
    AssetManager assets{std::filesystem::path{"."}};

    ObjectData obj;
    obj.components.push_back(MakeMeshRenderer(""));

    auto built = BuildSceneObject(obj, &assets);
    ASSERT_NE(built, nullptr);
    auto* mr = built->FindComponent<MeshRendererComponent>();
    ASSERT_NE(mr, nullptr);
    // headless では Builtin("cube") も nullptr になり cube 比較は素通りする
    // ResolveMeshFromRef が空参照で nullptr を返すことだけは headless でも確認できる
    EXPECT_EQ(ResolveMeshFromRef(assets, ""), nullptr);
    EXPECT_EQ(mr->GetMesh(), assets.Builtin("cube"));
}

// ".." で ContentRoot の外へ出る参照は弾かれる
TEST(MeshRefResolution, TraversalRefIsRejected)
{
    EXPECT_FALSE(ResolveContentPath("../secret.gltf").has_value());

    AssetManager assets{std::filesystem::path{"."}};
    EXPECT_EQ(ResolveMeshFromRef(assets, "../secret.gltf"), nullptr);
}

// 普通の相対パスは ContentRoot 配下の GetOrLoadMesh に回る
TEST(MeshRefResolution, RelativePathAttemptsContentRootLoad)
{
    const auto resolved = ResolveContentPath("meshes/foo.gltf");
    ASSERT_TRUE(resolved.has_value());

    AssetManager assets{std::filesystem::path{"."}};
    // ファイルが無いのでどちらも nullptr。GetOrLoadMesh に回っていることだけ見る
    EXPECT_EQ(ResolveMeshFromRef(assets, "meshes/foo.gltf"), assets.GetOrLoadMesh(*resolved));
}

// メッシュ参照が空の grid cube も cube に解決される
TEST(MeshRefResolution, ComponentsDrivenWithoutMeshRefResolvesCube)
{
    AssetManager assets{std::filesystem::path{"."}};

    ObjectData compObj;
    compObj.components.push_back(MakeMeshRenderer(""));

    auto compBuilt = BuildSceneObject(compObj, &assets);
    ASSERT_NE(compBuilt, nullptr);

    auto* compMesh = compBuilt->FindComponent<MeshRendererComponent>();
    ASSERT_NE(compMesh, nullptr);
    // 上と同じく ResolveMeshFromRef の nullptr だけ headless で確認する。cube 比較は device がある時だけ効く
    EXPECT_EQ(ResolveMeshFromRef(assets, ""), nullptr);
    EXPECT_EQ(compMesh->GetMesh(), assets.Builtin("cube"));
}
