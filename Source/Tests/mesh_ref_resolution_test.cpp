#include <Runtime/Platform/Filesystem.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Graphics/StaticMesh.h>
#include <Runtime/Object/AssetManager.h>
#include <Runtime/Object/Components/MeshColliderComponent.h>
#include <Runtime/Object/Components/MeshRendererComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/ObjectList.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Reflection/ObjectBuilder.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Scene/SceneData.h>
#include <Runtime/Physics/MeshCollision.h>
#include <Runtime/Physics/PhysicsScene.h>
#include <Runtime/Platform/Window.h>
#include <gtest/gtest.h>
#include <string>
#include <utility>

namespace
{
    using NS::Object::AssetManager;
    using NS::Object::BuildSceneObject;
    using NS::Object::MeshRendererComponent;
    using NS::Object::ObjectData;
    using NS::Object::ResolveContentPath;
    using NS::Object::ResolveMeshFromRef;

    // MeshRendererComponent 1 件分を作る。meshRef が空でなければ "メッシュ" フィールドに入れる
    nlohmann::json MakeMeshRenderer(const std::string& meshRef)
    {
        nlohmann::json c = NS::Object::MakeComponentEntry("MeshRendererComponent");
        if (!meshRef.empty())
            NS::Object::SetField(c, "メッシュ", meshRef);
        return c;
    }

    NS::Platform::WindowDesc MakeWindowDesc(const char* title)
    {
        NS::Platform::WindowDesc d{};
        d.title = title;
        d.size = NS::Core::Size2D{320, 240};
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
// builtin 登録には device が要るので、Renderer を作れない環境では飛ばす
TEST(MeshRefResolution, BuiltinNameResolvesToBuiltinMesh)
{
    NS::Platform::Window window(MakeWindowDesc("ns_meshref_builtin"));
    ASSERT_TRUE(window.IsValid());
    NS::Graphics::Renderer renderer(MakeRendererDesc(), window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "Device 確立不可 (headless)";

    AssetManager assets{NS::Platform::FileSystem::ContentRoot()};
    assets.RegisterBuiltins();

    ASSERT_NE(assets.Builtin("cube"), nullptr);
    EXPECT_EQ(ResolveMeshFromRef(assets, "cube"), assets.Builtin("cube"));
    EXPECT_EQ(ResolveMeshFromRef(assets, "wedge45"), assets.Builtin("wedge45"));
}

// メッシュ参照が空なら cube へフォールバックする
TEST(MeshRefResolution, EmptyMeshRefFallsBackToCube)
{
    AssetManager assets{std::string{"."}};

    ObjectData obj;
    obj.components.push_back(MakeMeshRenderer(""));

    auto built = BuildSceneObject(obj, &assets);
    ASSERT_NE(built, nullptr);
    auto* mr = built->FindComponent<MeshRendererComponent>();
    ASSERT_NE(mr, nullptr);
    // この assets は RegisterBuiltins を呼んでいないので Builtin("cube") は nullptr
    // cube 比較は両辺 nullptr で素通りし、確かめているのは空参照の nullptr だけ
    EXPECT_EQ(ResolveMeshFromRef(assets, ""), nullptr);
    EXPECT_EQ(mr->GetMesh(), assets.Builtin("cube"));
}

// ".." で ContentRoot の外へ出る参照は弾かれる
TEST(MeshRefResolution, TraversalRefIsRejected)
{
    EXPECT_FALSE(ResolveContentPath("../secret.gltf").has_value());

    AssetManager assets{std::string{"."}};
    EXPECT_EQ(ResolveMeshFromRef(assets, "../secret.gltf"), nullptr);
}

// 普通の相対パスは ContentRoot 配下の GetOrLoadMesh に回る
TEST(MeshRefResolution, RelativePathAttemptsContentRootLoad)
{
    const auto resolved = ResolveContentPath("meshes/foo.gltf");
    ASSERT_TRUE(resolved.has_value());

    AssetManager assets{std::string{"."}};
    // ファイルが無いのでどちらも nullptr。GetOrLoadMesh に回っていることだけ見る
    EXPECT_EQ(ResolveMeshFromRef(assets, "meshes/foo.gltf"), assets.GetOrLoadMesh(*resolved));
}

// メッシュ参照が空の object も cube に解決される
TEST(MeshRefResolution, ComponentsDrivenWithoutMeshRefResolvesCube)
{
    AssetManager assets{std::string{"."}};

    ObjectData compObj;
    compObj.components.push_back(MakeMeshRenderer(""));

    auto compBuilt = BuildSceneObject(compObj, &assets);
    ASSERT_NE(compBuilt, nullptr);

    auto* compMesh = compBuilt->FindComponent<MeshRendererComponent>();
    ASSERT_NE(compMesh, nullptr);
    // 上と同じく確かめているのは ResolveMeshFromRef の nullptr だけ
    // cube 比較は RegisterBuiltins を呼んだ assets でしか効かない
    EXPECT_EQ(ResolveMeshFromRef(assets, ""), nullptr);
    EXPECT_EQ(compMesh->GetMesh(), assets.Builtin("cube"));
}

// MeshColliderComponent は同じ object の MeshRendererComponent の参照から AssetManager の当たりを借りる
TEST(MeshRefResolution, MeshColliderTakesTrianglesFromRendererMesh)
{
    AssetManager assets{std::string{"."}};

    ObjectData obj;
    obj.components.push_back(MakeMeshRenderer("wedge45"));
    obj.components.push_back(NS::Object::MakeComponentEntry("MeshColliderComponent"));

    auto built = BuildSceneObject(obj, &assets);
    ASSERT_NE(built, nullptr);
    auto* collider = built->FindComponent<NS::Object::MeshColliderComponent>();
    ASSERT_NE(collider, nullptr);

    const NS::Physics::MeshCollision* wedge = assets.GetOrLoadMeshCollision("wedge45");
    ASSERT_NE(wedge, nullptr);
    EXPECT_EQ(collider->Collision(), wedge);
}

// 描画が cube へフォールバックする参照では、当たりも組み込みの cube の当たりを指す
TEST(MeshRefResolution, MeshColliderFallsBackToCubeLikeRenderer)
{
    AssetManager assets{std::string{"."}};

    ObjectData obj;
    obj.components.push_back(MakeMeshRenderer("__ns_missing_mesh__.gltf"));
    obj.components.push_back(NS::Object::MakeComponentEntry("MeshColliderComponent"));

    auto built = BuildSceneObject(obj, &assets);
    ASSERT_NE(built, nullptr);
    auto* collider = built->FindComponent<NS::Object::MeshColliderComponent>();
    ASSERT_NE(collider, nullptr);
    ASSERT_NE(collider->Collision(), nullptr);
    EXPECT_EQ(collider->Collision(), assets.GetOrLoadMeshCollision("cube"));
}

// MeshRendererComponent が無ければ当たり無しのまま
TEST(MeshRefResolution, MeshColliderWithoutRendererStaysEmpty)
{
    AssetManager assets{std::string{"."}};

    ObjectData obj;
    obj.components.push_back(NS::Object::MakeComponentEntry("MeshColliderComponent"));

    auto built = BuildSceneObject(obj, &assets);
    ASSERT_NE(built, nullptr);
    auto* collider = built->FindComponent<NS::Object::MeshColliderComponent>();
    ASSERT_NE(collider, nullptr);
    EXPECT_EQ(collider->Collision(), nullptr);
}

// 組んだ cube の当たりは body 1 個として physics に入り、下向きのレイが上面 (y = 0.5) で止まる
TEST(MeshRefResolution, BuiltCubeMeshColliderStopsRayAtTopFace)
{
    AssetManager assets{std::string{"."}};

    ObjectData obj;
    obj.components.push_back(MakeMeshRenderer("cube"));
    obj.components.push_back(NS::Object::MakeComponentEntry("MeshColliderComponent"));

    NS::Object::Scene scene;
    NS::Object::ObjectList objects;
    NS::Object::GameObject* placed = objects.Append(BuildSceneObject(obj, &assets));
    ASSERT_NE(placed, nullptr);
    // collider は持ち主の Scene の PhysicsScene しか受け取らないので、組んだ配置物を Scene へ結ぶ
    placed->AttachScene(&scene);

    NS::Physics::PhysicsScene& physics = scene.Physics();
    objects.SyncPhysics(physics);
    ASSERT_EQ(physics.BodyCount(), 1u);

    float distance = 0.0f;
    ASSERT_TRUE(
        physics.Raycast(NS::Core::Vector3{0.1f, 2.0f, 0.2f}, NS::Core::Vector3{0.0f, -1.0f, 0.0f}, 8.0f, distance));
    EXPECT_NEAR(distance, 1.5f, 1.0e-3f);
}
