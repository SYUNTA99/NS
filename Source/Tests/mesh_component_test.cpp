#include <gtest/gtest.h>

#include <ns/app/scene.h>
#include <ns/scene/components/mesh_component.h>
#include <ns/scene/game_object.h>
#include <ns/scene/i_renderable.h>
#include <ns/scene/render_context.h>

#include <vector>

namespace
{
    using ns::scene::MeshComponent;
    using ns::scene::GameObject;

    class FakeScene : public ns::app::Scene
    {
    public:
        void RegisterRenderable(ns::scene::IRenderable* renderable) override { registered.push_back(renderable); }
        void UnregisterRenderable(ns::scene::IRenderable* renderable) override { unregistered.push_back(renderable); }

        std::vector<ns::scene::IRenderable*> registered;
        std::vector<ns::scene::IRenderable*> unregistered;
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
    ns::scene::RenderContext ctx{};
    mc.Draw(ctx); // crash しなければ OK
    SUCCEED();
}

TEST(MeshComponentTest, DrawIsNoOpWhenMeshOrMaterialIsNull)
{
    GameObject obj;
    MeshComponent mc(nullptr, nullptr);
    obj.RegisterComponent(&mc);

    ns::scene::RenderContext ctx{};
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
    EXPECT_EQ(scene.registered[0], static_cast<ns::scene::IRenderable*>(&mc));
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
    EXPECT_EQ(scene.unregistered[0], static_cast<ns::scene::IRenderable*>(&mc));
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
