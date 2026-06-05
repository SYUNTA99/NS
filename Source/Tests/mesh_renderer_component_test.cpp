#include <gtest/gtest.h>

#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/IRenderable.h>
#include <Framework/Scene/MeshRendererComponent.h>
#include <Framework/Scene/RenderContext.h>
#include <Framework/Scene/SceneBase.h>

#include <vector>

namespace
{
    using NS::Scene::MeshRendererComponent;
    using NS::Scene::GameObject;

    class FakeScene : public NS::Scene::SceneBase
    {
    public:
        void RegisterRenderable(NS::Scene::IRenderable* renderable) override { registered.push_back(renderable); }
        void UnregisterRenderable(NS::Scene::IRenderable* renderable) override { unregistered.push_back(renderable); }

        std::vector<NS::Scene::IRenderable*> registered;
        std::vector<NS::Scene::IRenderable*> unregistered;
    };
} // namespace

TEST(MeshRendererComponentTest, ConstructsWithNullPointersWithoutCrashing)
{
    // Mesh / Material は呼出側保証。null でも構築時 crash しないこと
    MeshRendererComponent mc(nullptr, nullptr);
    EXPECT_TRUE(mc.IsActive());
}

TEST(MeshRendererComponentTest, DrawIsNoOpWhenInactive)
{
    GameObject obj;
    auto& mc = *obj.AddComponent<MeshRendererComponent>(nullptr, nullptr);
    mc.SetActive(false);

    // ctx を最小限で作って Draw 呼出。null mesh/material でもガード経由で no-op
    NS::Scene::RenderContext ctx{};
    mc.Draw(ctx); // crash しなければ OK
    SUCCEED();
}

TEST(MeshRendererComponentTest, DrawIsNoOpWhenMeshOrMaterialIsNull)
{
    GameObject obj;
    auto& mc = *obj.AddComponent<MeshRendererComponent>(nullptr, nullptr);

    NS::Scene::RenderContext ctx{};
    mc.Draw(ctx); // null ガードで no-op
    SUCCEED();
}

TEST(MeshRendererComponentTest, SetLightDirectionAndBaseColorDoNotCrash)
{
    MeshRendererComponent mc(nullptr, nullptr);
    mc.SetLightDirection({1.0f, 0.0f, 0.0f});
    mc.SetBaseColor({0.5f, 0.5f, 0.5f});
    // getter は提供していない。setter 呼出が crash せず IsActive を破壊しないことのみ verify
    EXPECT_TRUE(mc.IsActive());
}

TEST(MeshRendererComponentTest, OnStartRegistersToOwningScene)
{
    FakeScene scene;
    GameObject obj;
    obj.AttachScene(&scene);
    auto& mc = *obj.AddComponent<MeshRendererComponent>(nullptr, nullptr);

    mc.OnStart();

    ASSERT_EQ(scene.registered.size(), 1u);
    EXPECT_EQ(scene.registered[0], static_cast<NS::Scene::IRenderable*>(&mc));
}

TEST(MeshRendererComponentTest, OnEndPlayUnregistersFromOwningScene)
{
    FakeScene scene;
    GameObject obj;
    obj.AttachScene(&scene);
    auto& mc = *obj.AddComponent<MeshRendererComponent>(nullptr, nullptr);

    mc.OnStart();
    mc.OnEndPlay();

    ASSERT_EQ(scene.unregistered.size(), 1u);
    EXPECT_EQ(scene.unregistered[0], static_cast<NS::Scene::IRenderable*>(&mc));
}

TEST(MeshRendererComponentTest, OnStartIsNoOpWhenSceneIsNull)
{
    GameObject obj;
    auto& mc = *obj.AddComponent<MeshRendererComponent>(nullptr, nullptr);
    // OwningScene が nullptr のまま OnStart を呼んでも crash しないこと
    mc.OnStart();
    SUCCEED();
}
