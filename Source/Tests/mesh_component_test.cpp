#include <gtest/gtest.h>

#include <ns/scene/components/mesh_component.h>
#include <ns/scene/game_object.h>
#include <ns/scene/render_context.h>

namespace
{
    using ns::scene::MeshComponent;
    using ns::scene::GameObject;
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
