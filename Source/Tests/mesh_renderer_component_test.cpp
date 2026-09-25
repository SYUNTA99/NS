#include <Runtime/Graphics/RenderContext.h>
#include <Runtime/Graphics/RenderSettings.h>
#include <Runtime/Object/Components/MeshRenderer.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/IRenderable.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Transform.h>
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

namespace
{
    using NS::Obj::MeshRenderer;
    using NS::Obj::GameObject;
    using NS::Core::Matrix;
    using NS::Core::Quaternion;
    using NS::Core::Vector3;

    // Y 軸まわりに度で回す回転
    [[nodiscard]] Quaternion YawDegrees(float degrees)
    {
        return Quaternion::CreateFromAxisAngle(Vector3{0.0f, 1.0f, 0.0f},
                                               NS::Core::ToRadians(NS::Core::Degrees{degrees}).value);
    }

    class FakeScene : public NS::Obj::Scene
    {
    public:
        void RegisterRenderable(NS::Obj::IRenderable* renderable) override { registered.push_back(renderable); }
        void UnregisterRenderable(NS::Obj::IRenderable* renderable) override { unregistered.push_back(renderable); }

        std::vector<NS::Obj::IRenderable*> registered;
        std::vector<NS::Obj::IRenderable*> unregistered;
    };
} // namespace

TEST(MeshRendererComponentTest, ConstructsWithNullPointersWithoutCrashing)
{
    // null の Mesh / Material を渡しても構築で落ちないこと
    MeshRenderer mc;
    EXPECT_TRUE(mc.IsActive());
}

TEST(MeshRendererComponentTest, CollectIsNoOpWhenInactive)
{
    GameObject obj;
    MeshRenderer& mc = *obj.AddComponent<MeshRenderer>();
    mc.SetActive(false);

    // 非アクティブなら DrawItem を積まない
    NS::Gfx::RenderContext ctx{};
    std::vector<NS::Gfx::DrawItem> out;
    mc.Collect(ctx, out);
    EXPECT_TRUE(out.empty());
}

TEST(MeshRendererComponentTest, CollectIsNoOpWhenMeshOrMaterialIsNull)
{
    GameObject obj;
    MeshRenderer& mc = *obj.AddComponent<MeshRenderer>();

    NS::Gfx::RenderContext ctx{};
    std::vector<NS::Gfx::DrawItem> out;
    mc.Collect(ctx, out); // null ガードで何も積まない
    EXPECT_TRUE(out.empty());
}

TEST(MeshRendererComponentTest, SetBaseColorDoesNotAffectActive)
{
    MeshRenderer mc;
    mc.SetBaseColor({0.5f, 0.5f, 0.5f});
    EXPECT_TRUE(mc.IsActive());
}

TEST(MeshRendererComponentTest, OnStartRegistersToOwningScene)
{
    FakeScene scene;
    GameObject obj;
    obj.AttachScene(&scene);
    MeshRenderer& mc = *obj.AddComponent<MeshRenderer>();

    mc.OnStart();

    ASSERT_EQ(scene.registered.size(), 1u);
    EXPECT_EQ(scene.registered[0], static_cast<NS::Obj::IRenderable*>(&mc));
}

TEST(MeshRendererComponentTest, OnEndPlayUnregistersFromOwningScene)
{
    FakeScene scene;
    GameObject obj;
    obj.AttachScene(&scene);
    MeshRenderer& mc = *obj.AddComponent<MeshRenderer>();

    mc.OnStart();
    mc.OnEndPlay();

    ASSERT_EQ(scene.unregistered.size(), 1u);
    EXPECT_EQ(scene.unregistered[0], static_cast<NS::Obj::IRenderable*>(&mc));
}

TEST(MeshRendererComponentTest, OnStartIsNoOpWhenSceneIsNull)
{
    GameObject obj;
    MeshRenderer& mc = *obj.AddComponent<MeshRenderer>();
    // OwningScene が nullptr のまま OnStart を呼んでも落ちないこと
    mc.OnStart();
    SUCCEED();
}

// 局所の回転は根の行列より先に掛かる。先に回してから世界の軸で縮むので、潰れの軸が一緒に回らない
TEST(MeshRendererComponentTest, LocalRotationIsAppliedBeforeTheRootMatrix)
{
    GameObject obj;
    MeshRenderer& mc = *obj.AddComponent<MeshRenderer>();
    obj.Root().SetScale(Vector3{0.5f, 1.0f, 1.0f});
    mc.SetLocalRotation(YawDegrees(90.0f));

    const Vector3 moved = Vector3::Transform(Vector3{0.0f, 0.0f, 1.0f}, mc.DrawWorldMatrix(1.0f));

    // 掛ける順が逆だと、縮めてから回すので x は 1.0 になる
    EXPECT_NEAR(std::fabs(moved.x), 0.5f, 1e-5f);
    EXPECT_NEAR(moved.z, 0.0f, 1e-5f);
}

// 前のフレームの回転は OnUpdate で控え、描く時は前から今へ補間する
TEST(MeshRendererComponentTest, DrawWorldMatrixInterpolatesThePreviousAndCurrentLocalRotation)
{
    GameObject obj;
    MeshRenderer& mc = *obj.AddComponent<MeshRenderer>();
    mc.SetLocalRotation(YawDegrees(20.0f));
    mc.OnUpdate();
    mc.SetLocalRotation(YawDegrees(80.0f));

    const Vector3 forward{0.0f, 0.0f, 1.0f};
    const Vector3 atPrevious = Vector3::Transform(forward, mc.DrawWorldMatrix(0.0f));
    const Vector3 atHalf = Vector3::Transform(forward, mc.DrawWorldMatrix(0.5f));
    const Vector3 atCurrent = Vector3::Transform(forward, mc.DrawWorldMatrix(1.0f));

    const float toRad = NS::Core::k_Pi / 180.0f;
    EXPECT_NEAR(atPrevious.x, std::sin(20.0f * toRad), 1e-5f);
    EXPECT_NEAR(atPrevious.z, std::cos(20.0f * toRad), 1e-5f);
    EXPECT_NEAR(atHalf.x, std::sin(50.0f * toRad), 1e-5f);
    EXPECT_NEAR(atHalf.z, std::cos(50.0f * toRad), 1e-5f);
    EXPECT_NEAR(atCurrent.x, std::sin(80.0f * toRad), 1e-5f);
    EXPECT_NEAR(atCurrent.z, std::cos(80.0f * toRad), 1e-5f);
}

// 局所の回転を書かない配置物は、今までどおり根の補間行列で描く
TEST(MeshRendererComponentTest, DrawWorldMatrixIsTheRootMatrixWhileTheLocalRotationIsIdentity)
{
    GameObject obj;
    MeshRenderer& mc = *obj.AddComponent<MeshRenderer>();
    obj.Root().SetPosition(Vector3{1.0f, 2.0f, 3.0f});
    obj.Root().SetScale(Vector3{1.0f, 0.9f, 1.0f});
    obj.Root().Snapshot();
    obj.Root().SetPosition(Vector3{4.0f, 2.5f, -1.0f});
    obj.Root().SetRotation(YawDegrees(30.0f));

    const Matrix drawn = mc.DrawWorldMatrix(0.25f);
    const Matrix root = obj.Root().InterpolatedWorldMatrix(0.25f);

    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            EXPECT_NEAR(drawn.m[row][column], root.m[row][column], 1e-6f) << row << "," << column;
        }
    }
}
