#include <gtest/gtest.h>

#include <Framework/Scene/RootScene.h>
#include <Framework/Scene/MeshComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/IRenderable.h>
#include <Framework/Scene/RenderContext.h>

#include <vector>

namespace
{
    using NS::Scene::MeshComponent;
    using NS::Scene::GameObject;

    class FakeScene : public NS::Scene::RootScene
    {
    public:
        void RegisterRenderable(NS::Scene::IRenderable* renderable) override { registered.push_back(renderable); }
        void UnregisterRenderable(NS::Scene::IRenderable* renderable) override { unregistered.push_back(renderable); }

        std::vector<NS::Scene::IRenderable*> registered;
        std::vector<NS::Scene::IRenderable*> unregistered;
    };
} // namespace

TEST(MeshComponentTest, ConstructsWithNullPointersWithoutCrashing)
{
    // Mesh / Material は呼出側保証。null でも構築時 crash しないこと。
    MeshComponent mc(nullptr, nullptr);
    EXPECT_TRUE(mc.IsActive());
}

TEST(MeshComponentTest, DrawIsNoOpWhenInactive)
{
    GameObject obj;
    MeshComponent mc(nullptr, nullptr);
    obj.RegisterComponent(&mc);
    mc.SetActive(false);

    // ctx を最小限で作って Draw 呼出。null mesh/material でもガード経由で no-op。
    NS::Scene::RenderContext ctx{};
    mc.Draw(ctx); // crash しなければ OK
    SUCCEED();
}

TEST(MeshComponentTest, DrawIsNoOpWhenMeshOrMaterialIsNull)
{
    GameObject obj;
    MeshComponent mc(nullptr, nullptr);
    obj.RegisterComponent(&mc);

    NS::Scene::RenderContext ctx{};
    mc.Draw(ctx); // null ガードで no-op
    SUCCEED();
}

TEST(MeshComponentTest, SetLightDirectionAndBaseColorDoNotCrash)
{
    MeshComponent mc(nullptr, nullptr);
    mc.SetLightDirection({1.0f, 0.0f, 0.0f});
    mc.SetBaseColor({0.5f, 0.5f, 0.5f});
    // getter は提供していない。setter 呼出が crash せず IsActive を破壊しないことのみ verify。
    EXPECT_TRUE(mc.IsActive());
}

TEST(MeshComponentTest, OnStartRegistersToOwningScene)
{
    FakeScene scene;
    GameObject obj;
    obj.AttachScene(&scene);
    MeshComponent mc(nullptr, nullptr);
    obj.RegisterComponent(&mc);

    mc.OnStart();

    ASSERT_EQ(scene.registered.size(), 1u);
    EXPECT_EQ(scene.registered[0], static_cast<NS::Scene::IRenderable*>(&mc));
}

TEST(MeshComponentTest, OnEndPlayUnregistersFromOwningScene)
{
    FakeScene scene;
    GameObject obj;
    obj.AttachScene(&scene);
    MeshComponent mc(nullptr, nullptr);
    obj.RegisterComponent(&mc);

    mc.OnStart();
    mc.OnEndPlay();

    ASSERT_EQ(scene.unregistered.size(), 1u);
    EXPECT_EQ(scene.unregistered[0], static_cast<NS::Scene::IRenderable*>(&mc));
}

TEST(MeshComponentTest, OnStartIsNoOpWhenSceneIsNull)
{
    GameObject obj;
    MeshComponent mc(nullptr, nullptr);
    obj.RegisterComponent(&mc);
    // OwningScene が nullptr のまま OnStart を呼んでも crash しないこと。
    mc.OnStart();
    SUCCEED();
}
