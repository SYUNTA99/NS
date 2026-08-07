#include <Runtime/Graphics/RenderContext.h>
#include <Runtime/Graphics/RenderSettings.h>
#include <Runtime/Object/Components/MeshRendererComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/IRenderable.h>
#include <Runtime/Object/Scene/Scene.h>
#include <gtest/gtest.h>
#include <vector>

namespace
{
    using NS::Object::MeshRendererComponent;
    using NS::Object::GameObject;

    class FakeScene : public NS::Object::Scene
    {
    public:
        void RegisterRenderable(NS::Object::IRenderable* renderable) override { registered.push_back(renderable); }
        void UnregisterRenderable(NS::Object::IRenderable* renderable) override { unregistered.push_back(renderable); }

        std::vector<NS::Object::IRenderable*> registered;
        std::vector<NS::Object::IRenderable*> unregistered;
    };
} // namespace

TEST(MeshRendererComponentTest, ConstructsWithNullPointersWithoutCrashing)
{
    // null の Mesh / Material を渡しても構築で落ちないこと
    MeshRendererComponent mc;
    EXPECT_TRUE(mc.IsActive());
}

TEST(MeshRendererComponentTest, CollectIsNoOpWhenInactive)
{
    GameObject obj;
    auto& mc = *obj.AddComponent<MeshRendererComponent>();
    mc.SetActive(false);

    // 非アクティブなら DrawItem を積まない
    NS::Graphics::RenderContext ctx{};
    std::vector<NS::Graphics::DrawItem> out;
    mc.Collect(ctx, out);
    EXPECT_TRUE(out.empty());
}

TEST(MeshRendererComponentTest, CollectIsNoOpWhenMeshOrMaterialIsNull)
{
    GameObject obj;
    auto& mc = *obj.AddComponent<MeshRendererComponent>();

    NS::Graphics::RenderContext ctx{};
    std::vector<NS::Graphics::DrawItem> out;
    mc.Collect(ctx, out); // null ガードで何も積まない
    EXPECT_TRUE(out.empty());
}

TEST(MeshRendererComponentTest, SetBaseColorDoesNotAffectActive)
{
    MeshRendererComponent mc;
    mc.SetBaseColor({0.5f, 0.5f, 0.5f});
    EXPECT_TRUE(mc.IsActive());
}

TEST(MeshRendererComponentTest, OnStartRegistersToOwningScene)
{
    FakeScene scene;
    GameObject obj;
    obj.AttachScene(&scene);
    auto& mc = *obj.AddComponent<MeshRendererComponent>();

    mc.OnStart();

    ASSERT_EQ(scene.registered.size(), 1u);
    EXPECT_EQ(scene.registered[0], static_cast<NS::Object::IRenderable*>(&mc));
}

TEST(MeshRendererComponentTest, OnEndPlayUnregistersFromOwningScene)
{
    FakeScene scene;
    GameObject obj;
    obj.AttachScene(&scene);
    auto& mc = *obj.AddComponent<MeshRendererComponent>();

    mc.OnStart();
    mc.OnEndPlay();

    ASSERT_EQ(scene.unregistered.size(), 1u);
    EXPECT_EQ(scene.unregistered[0], static_cast<NS::Object::IRenderable*>(&mc));
}

TEST(MeshRendererComponentTest, OnStartIsNoOpWhenSceneIsNull)
{
    GameObject obj;
    auto& mc = *obj.AddComponent<MeshRendererComponent>();
    // OwningScene が nullptr のまま OnStart を呼んでも落ちないこと
    mc.OnStart();
    SUCCEED();
}
