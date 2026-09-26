#include <Runtime/Core/AABB.h>
#include <Runtime/Graphics/RenderContext.h>
#include <Runtime/Graphics/RenderSettings.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Graphics/StaticMesh.h>
#include <Runtime/Object/AssetManager.h>
#include <Runtime/Object/Components/MeshRenderer.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/IRenderable.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Platform/Filesystem.h>
#include <Runtime/Platform/Window.h>
#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
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

    // 描く形の局所の境界。中心が原点で半分の幅 0.65 の玉を包む箱
    const NS::Core::AABB k_BallBounds{Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.65f, 0.65f, 0.65f}};

    // 箱の角 8 つと中心。倍率が形のどこにどう掛かるかを見る点
    [[nodiscard]] std::array<Vector3, 9> BoundsPoints(const NS::Core::AABB& bounds)
    {
        const Vector3 c{bounds.Center.x, bounds.Center.y, bounds.Center.z};
        const Vector3 e{bounds.Extents.x, bounds.Extents.y, bounds.Extents.z};
        return {c,
                c + Vector3{-e.x, -e.y, -e.z},
                c + Vector3{e.x, -e.y, -e.z},
                c + Vector3{-e.x, e.y, -e.z},
                c + Vector3{e.x, e.y, -e.z},
                c + Vector3{-e.x, -e.y, e.z},
                c + Vector3{e.x, -e.y, e.z},
                c + Vector3{-e.x, e.y, e.z},
                c + Vector3{e.x, e.y, e.z}};
    }

    // 描く行列の各点が、倍率の無い行列の点を pivot から世界の軸ごとに scale 倍した所にあるか
    void ExpectScaledAboutThePivot(
        const MeshRenderer& mc, const Matrix& unscaled, const Vector3& pivot, const Vector3& scale, float alpha)
    {
        const Matrix drawn = mc.DrawWorldMatrix(alpha);
        for (const Vector3& point : BoundsPoints(k_BallBounds))
        {
            const Vector3 before = Vector3::Transform(point, unscaled);
            const Vector3 after = Vector3::Transform(point, drawn);
            EXPECT_NEAR(after.x, pivot.x + (before.x - pivot.x) * scale.x, 1e-5f);
            EXPECT_NEAR(after.y, pivot.y + (before.y - pivot.y) * scale.y, 1e-5f);
            EXPECT_NEAR(after.z, pivot.z + (before.z - pivot.z) * scale.z, 1e-5f);
        }
    }

    // 2 つの行列が 1 ビットも違わないか
    void ExpectSameBits(const Matrix& actual, const Matrix& expected)
    {
        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 4; ++column)
            {
                EXPECT_EQ(actual.m[row][column], expected.m[row][column]) << row << "," << column;
            }
        }
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

// 倍率は描く形の下端の真ん中を中心に掛かる。縦に潰すと下端の高さは変わらず、上端だけが下がる
TEST(MeshRendererComponentTest, DrawScaleKeepsTheBottomOfTheDrawnShapeAndLowersTheTop)
{
    GameObject obj;
    MeshRenderer& mc = *obj.AddComponent<MeshRenderer>();
    mc.SetLocalBoundsOverride(k_BallBounds);
    obj.Root().SetPosition(Vector3{1.0f, 2.0f, 3.0f});

    EXPECT_TRUE(mc.SetDrawScale(Vector3{1.0f, 0.8f, 1.0f}));

    const Vector3 bottom = Vector3::Transform(Vector3{0.0f, -0.65f, 0.0f}, mc.DrawWorldMatrix(1.0f));
    const Vector3 top = Vector3::Transform(Vector3{0.0f, 0.65f, 0.0f}, mc.DrawWorldMatrix(1.0f));
    EXPECT_NEAR(bottom.y, 2.0f - 0.65f, 1e-5f);
    EXPECT_NEAR(top.y, 2.0f - 0.65f + 1.3f * 0.8f, 1e-5f);
    EXPECT_NEAR(top.x, 1.0f, 1e-5f);
    EXPECT_NEAR(top.z, 3.0f, 1e-5f);
}

// 局所の回転があっても、倍率は回した後の世界の縦に掛かる。回っている玉も床に対して潰れる
TEST(MeshRendererComponentTest, DrawScaleSquashesAlongTheWorldUpEvenWithALocalRotation)
{
    GameObject obj;
    MeshRenderer& mc = *obj.AddComponent<MeshRenderer>();
    mc.SetLocalBoundsOverride(k_BallBounds);
    obj.Root().SetPosition(Vector3{0.0f, 1.15f, 0.0f});
    const Quaternion tilt = Quaternion::CreateFromAxisAngle(Vector3{1.0f, 0.0f, 1.0f} / std::sqrt(2.0f), 0.7f);
    mc.SnapLocalRotation(tilt);
    const Matrix unscaled = mc.DrawWorldMatrix(1.0f);

    const Vector3 scale{1.0f, 0.5f, 1.0f};
    EXPECT_TRUE(mc.SnapDrawScale(scale));

    // 中心は回す前の玉の下端の真ん中。玉より下へ出る、回した箱を包む箱の下端ではない
    ExpectScaledAboutThePivot(mc, unscaled, Vector3{0.0f, 0.5f, 0.0f}, scale, 1.0f);
}

// 根に回転とスケールがあっても、倍率は根の軸でなく世界の軸に掛かる
TEST(MeshRendererComponentTest, DrawScaleActsOnTheWorldAxesEvenWithARotatedAndScaledRoot)
{
    GameObject obj;
    MeshRenderer& mc = *obj.AddComponent<MeshRenderer>();
    mc.SetLocalBoundsOverride(k_BallBounds);
    obj.Root().SetPosition(Vector3{2.0f, 3.0f, -1.0f});
    obj.Root().SetScale(Vector3{2.0f, 1.0f, 1.0f});
    // z まわりに 90 度。根の x (2 倍) が世界の縦へ向く
    obj.Root().SetRotation(Quaternion::CreateFromAxisAngle(Vector3{0.0f, 0.0f, 1.0f}, NS::Core::k_Pi * 0.5f));
    const Matrix unscaled = mc.DrawWorldMatrix(1.0f);

    const Vector3 scale{1.5f, 0.5f, 1.0f};
    EXPECT_TRUE(mc.SnapDrawScale(scale));

    // 世界で見た形の縦の半分は 0.65 × 2 = 1.3。下端の真ん中は根の真下 1.3 m
    ExpectScaledAboutThePivot(mc, unscaled, Vector3{2.0f, 3.0f - 1.3f, -1.0f}, scale, 1.0f);
}

// 前のフレームの倍率は OnUpdate で控え、描く時は前から今へ補間する。SnapDrawScale は前と今を揃える
TEST(MeshRendererComponentTest, DrawScaleInterpolatesThePreviousAndCurrentAndSnapAlignsThem)
{
    GameObject obj;
    MeshRenderer& mc = *obj.AddComponent<MeshRenderer>();
    mc.SetLocalBoundsOverride(k_BallBounds);
    // 根は前と今を同じ位置に揃え、補間で動くのを倍率だけにする
    obj.Root().SetPosition(Vector3{0.0f, 1.15f, 0.0f});
    obj.Root().Snapshot();
    const Matrix unscaled = mc.DrawWorldMatrix(1.0f);
    const Vector3 pivot{0.0f, 0.5f, 0.0f};

    ASSERT_TRUE(mc.SetDrawScale(Vector3{1.2f, 0.6f, 1.2f}));
    mc.OnUpdate();
    ASSERT_TRUE(mc.SetDrawScale(Vector3{1.0f, 0.8f, 1.0f}));

    ExpectScaledAboutThePivot(mc, unscaled, pivot, Vector3{1.2f, 0.6f, 1.2f}, 0.0f);
    ExpectScaledAboutThePivot(mc, unscaled, pivot, Vector3{1.1f, 0.7f, 1.1f}, 0.5f);
    ExpectScaledAboutThePivot(mc, unscaled, pivot, Vector3{1.0f, 0.8f, 1.0f}, 1.0f);

    ASSERT_TRUE(mc.SnapDrawScale(Vector3{1.0f, 0.9f, 1.0f}));
    ExpectScaledAboutThePivot(mc, unscaled, pivot, Vector3{1.0f, 0.9f, 1.0f}, 0.0f);
    ExpectScaledAboutThePivot(mc, unscaled, pivot, Vector3{1.0f, 0.9f, 1.0f}, 1.0f);
}

// 根が前と今で違う高さにあっても、倍率の中心は補間した根から取る。描く形の下端は補間した根の下端のまま
TEST(MeshRendererComponentTest, DrawScaleKeepsTheBottomOfTheInterpolatedShapeWhileTheRootMoves)
{
    GameObject obj;
    MeshRenderer& mc = *obj.AddComponent<MeshRenderer>();
    mc.SetLocalBoundsOverride(k_BallBounds);
    obj.Root().SetPosition(Vector3{0.0f, 2.0f, 0.0f});
    obj.Root().Snapshot();
    obj.Root().SetPosition(Vector3{0.0f, 1.15f, 0.0f});

    ASSERT_TRUE(mc.SetDrawScale(Vector3{1.0f, 0.8f, 1.0f}));

    // 補間の割合 0.5 の根の高さは、前 2.0 と今 1.15 の真ん中
    const float rootHeight = obj.Root().InterpolatedWorldMatrix(0.5f)._42;
    ASSERT_NEAR(rootHeight, (2.0f + 1.15f) * 0.5f, 1e-5f);
    const Vector3 bottom = Vector3::Transform(Vector3{0.0f, -0.65f, 0.0f}, mc.DrawWorldMatrix(0.5f));
    EXPECT_NEAR(bottom.y, rootHeight - 0.65f, 1e-5f);
}

// 倍率が既定の (1, 1, 1) の間は、描く行列が局所の回転 × 根の補間行列と 1 ビットも違わない
// 一度潰して戻した後も同じ
TEST(MeshRendererComponentTest, DefaultDrawScaleLeavesTheDrawMatrixBitForBit)
{
    GameObject obj;
    MeshRenderer& mc = *obj.AddComponent<MeshRenderer>();
    mc.SetLocalBoundsOverride(k_BallBounds);
    // 根の高さが正で、下端の真ん中 (根の 0.585 m 下) が負になる高さを通す。足し引きで下の桁が丸まりやすい
    obj.Root().SetPosition(Vector3{1.0f, 0.1f, 3.0f});
    obj.Root().SetScale(Vector3{1.0f, 0.9f, 1.3f});
    obj.Root().Snapshot();
    obj.Root().SetPosition(Vector3{4.0f, 0.5f, -1.0f});
    obj.Root().SetRotation(YawDegrees(30.0f));
    const Quaternion previous = YawDegrees(20.0f);
    const Quaternion current = Quaternion::CreateFromAxisAngle(Vector3{1.0f, 0.0f, 0.0f}, 0.4f);
    mc.SetLocalRotation(previous);
    mc.OnUpdate();
    mc.SetLocalRotation(current);

    EXPECT_EQ(mc.DrawScale(), Vector3(1.0f, 1.0f, 1.0f));
    // 下端の真ん中へ移して戻す足し引きは、位置によって下の桁が丸まる。補間の割合を細かく振って拾う
    constexpr int k_AlphaSteps = 64;
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int step = 0; step <= k_AlphaSteps; ++step)
        {
            const float alpha = static_cast<float>(step) / static_cast<float>(k_AlphaSteps);
            const Matrix expected = Matrix::CreateFromQuaternion(Quaternion::Slerp(previous, current, alpha)) *
                                    obj.Root().InterpolatedWorldMatrix(alpha);
            ExpectSameBits(mc.DrawWorldMatrix(alpha), expected);
        }
        ASSERT_TRUE(mc.SnapDrawScale(Vector3{1.0f, 0.8f, 1.0f}));
        ASSERT_TRUE(mc.SnapDrawScale(Vector3{1.0f, 1.0f, 1.0f}));
    }
}

// 有限の正でない成分を含む倍率は断り、今の倍率も描く行列も変えない
TEST(MeshRendererComponentTest, RejectsNonFiniteAndNonPositiveDrawScales)
{
    GameObject obj;
    MeshRenderer& mc = *obj.AddComponent<MeshRenderer>();
    mc.SetLocalBoundsOverride(k_BallBounds);
    obj.Root().SetPosition(Vector3{0.0f, 1.15f, 0.0f});
    ASSERT_TRUE(mc.SnapDrawScale(Vector3{1.0f, 0.8f, 1.0f}));
    const Matrix before = mc.DrawWorldMatrix(0.5f);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    const std::array<Vector3, 5> rejected{Vector3{1.0f, nan, 1.0f},
                                          Vector3{infinity, 1.0f, 1.0f},
                                          Vector3{1.0f, 0.0f, 1.0f},
                                          Vector3{1.0f, 1.0f, -0.5f},
                                          Vector3{nan, nan, nan}};
    for (const Vector3& scale : rejected)
    {
        EXPECT_FALSE(mc.SetDrawScale(scale));
        EXPECT_FALSE(mc.SnapDrawScale(scale));
    }

    EXPECT_EQ(mc.DrawScale(), Vector3(1.0f, 0.8f, 1.0f));
    ExpectSameBits(mc.DrawWorldMatrix(0.5f), before);
}

namespace
{
    // 境界は mesh から取るので、mesh を GPU に作る device が要る。作れない環境では飛ばす
    class MeshRendererBoundsTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            NS::Platform::WindowDesc windowDesc{};
            windowDesc.title = "ns_mesh_renderer_bounds";
            windowDesc.size = NS::Core::Size2D{320, 240};
            windowDesc.visible = false;
            m_window = std::make_unique<NS::Platform::Window>(windowDesc);
            ASSERT_TRUE(m_window->IsValid());

            NS::Gfx::RendererDesc rendererDesc{};
            rendererDesc.vsync = false;
            rendererDesc.enableDebugLayer = false;
            m_renderer = std::make_unique<NS::Gfx::Renderer>(rendererDesc, *m_window);
            if (!m_renderer->IsValid())
            {
                GTEST_SKIP() << "Device 確立不可 (headless)";
            }
            m_assets = std::make_unique<NS::Obj::AssetManager>(NS::Platform::FileSystem::ContentRoot());
        }

        void TearDown() override
        {
            // GPU の資源は Renderer より先に手放す
            m_assets.reset();
            m_renderer.reset();
            m_window.reset();
        }

        std::unique_ptr<NS::Platform::Window> m_window;
        std::unique_ptr<NS::Gfx::Renderer> m_renderer;
        std::unique_ptr<NS::Obj::AssetManager> m_assets;
    };
} // namespace

// 間引きの境界は潰れた形を包む。下端はそのままで、前と今の倍率の大きい方まで広げる
TEST_F(MeshRendererBoundsTest, WorldBoundsWrapsTheSquashedShape)
{
    GameObject obj;
    MeshRenderer& mc = *obj.AddComponent<MeshRenderer>();
    NS::Gfx::StaticMesh* ball = m_assets->GetOrMakeCapsuleMesh(0.65f, 0.0f);
    ASSERT_NE(ball, nullptr);
    mc.SetMesh(ball);
    obj.Root().SetPosition(Vector3{0.0f, 1.15f, 0.0f});

    ASSERT_TRUE(mc.SnapDrawScale(Vector3{1.2f, 0.8f, 1.2f}));
    const NS::Core::AABB squashed = mc.WorldBounds();
    EXPECT_NEAR(squashed.Center.y - squashed.Extents.y, 0.5f, 1e-5f);
    EXPECT_NEAR(squashed.Center.y + squashed.Extents.y, 0.5f + 1.3f * 0.8f, 1e-5f);
    EXPECT_NEAR(squashed.Extents.x, 0.65f * 1.2f, 1e-5f);
    EXPECT_NEAR(squashed.Extents.z, 0.65f * 1.2f, 1e-5f);
    EXPECT_NEAR(squashed.Center.x, 0.0f, 1e-5f);

    // 戻る途中は前のフレームの形も描く。前が横に広く、今が縦に高い
    mc.OnUpdate();
    ASSERT_TRUE(mc.SetDrawScale(Vector3{1.0f, 0.9f, 1.0f}));
    const NS::Core::AABB recovering = mc.WorldBounds();
    EXPECT_NEAR(recovering.Center.y - recovering.Extents.y, 0.5f, 1e-5f);
    EXPECT_NEAR(recovering.Center.y + recovering.Extents.y, 0.5f + 1.3f * 0.9f, 1e-5f);
    EXPECT_NEAR(recovering.Extents.x, 0.65f * 1.2f, 1e-5f);
}
