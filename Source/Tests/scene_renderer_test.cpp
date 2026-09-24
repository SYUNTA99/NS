#include "pixel_readback.h"

#include <Runtime/Core/AABB.h>
#include <Runtime/Core/Logger.h>
#include <Runtime/Graphics/EffectScene.h>
#include <Runtime/Graphics/RenderContext.h>
#include <Runtime/Graphics/RenderTarget.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Object/Components/CameraBrain.h>
#include <Runtime/Object/Components/CameraComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/IRenderable.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Scene/SceneRenderer.h>
#include <Runtime/Platform/Clock.h>
#include <Runtime/Platform/Filesystem.h>
#include <Runtime/Platform/Window.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using NS::Gfx::EffectHandle;
    using NS::Gfx::RenderContext;
    using NS::Gfx::Renderer;
    using NS::Gfx::RendererDesc;
    using NS::Gfx::RenderTarget;
    using NS::Obj::CameraBrain;
    using NS::Obj::CameraComponent;
    using NS::Obj::CameraPose;
    using NS::Obj::GameObject;
    using NS::Obj::IRenderable;
    using NS::Obj::RenderBucket;
    using NS::Obj::Scene;
    using NS::Obj::SceneEnvironment;
    using NS::Obj::SceneRenderer;
    using NS::Obj::SceneView;
    using NS::Platform::FileSystem;
    using NS::Platform::Window;
    using NS::Platform::WindowDesc;
    using NS::Tests::LargestChannelDifference;
    using NS::Tests::PixelPosition;
    using NS::Tests::ReadPixel;

    constexpr float k_Frame = 1.0f / 60.0f;
    constexpr int k_WindowSize = 64;

    // square_r.efkefc の寿命は 100 フレーム。前後を跨ぐと在るか消えたかが分かれる
    constexpr int k_FramesBeforeLifeEnds = 80;
    constexpr int k_FramesAfterLifeEnds = 120;

    constexpr PixelPosition k_Center{k_WindowSize / 2, k_WindowSize / 2};

    // 背景と板 1 枚を分ける幅。半透明で薄まるので 255 は出ない
    constexpr int k_VisibleDifference = 8;

    WindowDesc MakeHiddenWindowDesc()
    {
        WindowDesc d{};
        d.title = "ns_scene_renderer_effect";
        d.size = NS::Core::Size2D{k_WindowSize, k_WindowSize};
        d.visible = false;
        return d;
    }

    RendererDesc MakeRendererDesc()
    {
        RendererDesc d{};
#ifdef NS_BUILD_DEBUG
        d.enableDebugLayer = true;
#else
        d.enableDebugLayer = false;
#endif
        d.vsync = false;
        return d;
    }

    std::string TestEffectRoot()
    {
        const std::string source = FileSystem::Combine(FileSystem::ContentRoot(), "Source");
        const std::string tests = FileSystem::Combine(source, "Tests");
        return FileSystem::Combine(FileSystem::Combine(tests, "data"), "effects");
    }

    class FakeRenderable : public IRenderable
    {
    public:
        FakeRenderable(int id, std::vector<int>* log) : m_id(id), m_log(log) {}

        void Collect(const RenderContext&, std::vector<NS::Gfx::DrawItem>&) override { m_log->push_back(m_id); }
        [[nodiscard]] RenderBucket Bucket() const noexcept override { return RenderBucket::Opaque; }
        [[nodiscard]] NS::Core::Vector3 SortCenter() const noexcept override { return NS::Core::Vector3{}; }
        [[nodiscard]] int SortPriority() const noexcept override { return 0; }
        [[nodiscard]] NS::Core::AABB WorldBounds() const noexcept override
        {
            // 視錐台に入れるため原点を囲む
            // 既定の RenderContext では視錐台がクリップ空間そのものになる
            return NS::Core::AABB{NS::Core::Vector3{}, NS::Core::Vector3{1.0f, 1.0f, 1.0f}};
        }

    private:
        int m_id;
        std::vector<int>* m_log;
    };
} // namespace

TEST(SceneRendererTest, DrawsRegisteredRenderableWithoutAScene)
{
    std::vector<int> log;
    FakeRenderable renderable{7, &log};

    SceneRenderer renderer;
    renderer.RegisterRenderable(&renderable);
    renderer.SyncRenderBounds();

    RenderContext ctx{};
    renderer.DrawOpaque(ctx);

    EXPECT_EQ(log.size(), 1u);
}

TEST(SceneRendererTest, UnregisteredRenderableIsNotDrawn)
{
    std::vector<int> log;
    FakeRenderable renderable{7, &log};

    SceneRenderer renderer;
    renderer.RegisterRenderable(&renderable);
    renderer.UnregisterRenderable(&renderable);
    renderer.SyncRenderBounds();

    RenderContext ctx{};
    renderer.DrawOpaque(ctx);

    EXPECT_TRUE(log.empty());
}

// EffectScene は構築時に Gpu() の device と context を読む。Renderer を立ててから差す
class SceneRendererEffectTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        NS::Core::Logger::Init();
        NS::Platform::FrameTimer::SetFixedDelta(k_Frame);
        m_window = std::make_unique<Window>(MakeHiddenWindowDesc());
        ASSERT_TRUE(m_window->IsValid());
        m_renderer = std::make_unique<Renderer>(MakeRendererDesc(), *m_window);
        ASSERT_TRUE(m_renderer->IsValid());
    }

    void TearDown() override
    {
        m_renderer.reset();
        m_window.reset();
        NS::Core::Logger::Shutdown();
    }

    std::unique_ptr<Window> m_window;
    std::unique_ptr<Renderer> m_renderer;
};

TEST_F(SceneRendererEffectTest, HasNoEffectSceneUntilRendererIsSet)
{
    SceneRenderer renderer;

    EXPECT_EQ(renderer.Effects(), nullptr);
}

TEST_F(SceneRendererEffectTest, BuildsEffectSceneWhenRendererIsSet)
{
    SceneRenderer renderer;
    renderer.SetRenderer(m_renderer.get());

    ASSERT_NE(renderer.Effects(), nullptr);
    EXPECT_TRUE(renderer.Effects()->IsValid());
}

TEST_F(SceneRendererEffectTest, DropsEffectSceneWhenRendererIsCleared)
{
    SceneRenderer renderer;
    renderer.SetRenderer(m_renderer.get());
    renderer.SetRenderer(nullptr);

    EXPECT_EQ(renderer.Effects(), nullptr);
}

TEST_F(SceneRendererEffectTest, PreloadsFromTheEffectRootThatWasSet)
{
    SceneRenderer renderer;
    renderer.SetEffectRoot(TestEffectRoot());
    renderer.SetRenderer(m_renderer.get());

    ASSERT_NE(renderer.Effects(), nullptr);
    EXPECT_TRUE(renderer.Effects()->Preload("square_r"));
}

// 上の試しは Preload が中身を見ずに真を返す実装でも通る。違う根では読めないことを別に見る
TEST_F(SceneRendererEffectTest, PreloadFailsWhenTheEffectRootIsWrong)
{
    SceneRenderer renderer;
    renderer.SetEffectRoot(FileSystem::Combine(FileSystem::ContentRoot(), "no_such_directory"));
    renderer.SetRenderer(m_renderer.get());

    ASSERT_NE(renderer.Effects(), nullptr);
    EXPECT_FALSE(renderer.Effects()->Preload("square_r"));
}

TEST_F(SceneRendererEffectTest, SceneOnUpdateAdvancesEffects)
{
    Scene scene;
    scene.SetEffectRoot(TestEffectRoot());
    scene.SetRenderer(m_renderer.get());
    ASSERT_NE(scene.Effects(), nullptr);
    ASSERT_TRUE(scene.Effects()->Preload("square_r"));

    const EffectHandle handle = scene.Effects()->Play("square_r");
    ASSERT_TRUE(handle.IsValid());

    for (int i = 0; i < k_FramesBeforeLifeEnds; ++i)
    {
        scene.OnUpdate();
    }
    EXPECT_TRUE(scene.Effects()->Exists(handle));

    for (int i = k_FramesBeforeLifeEnds; i < k_FramesAfterLifeEnds; ++i)
    {
        scene.OnUpdate();
    }
    EXPECT_FALSE(scene.Effects()->Exists(handle));
}

// 更新は時間停止の判定より後ろ。止めている間は寿命を過ぎても消えない
TEST_F(SceneRendererEffectTest, PausedSceneDoesNotAdvanceEffects)
{
    Scene scene;
    scene.SetEffectRoot(TestEffectRoot());
    scene.SetRenderer(m_renderer.get());
    ASSERT_NE(scene.Effects(), nullptr);
    ASSERT_TRUE(scene.Effects()->Preload("square_r"));

    const EffectHandle handle = scene.Effects()->Play("square_r");
    ASSERT_TRUE(handle.IsValid());
    scene.SetSimulationPaused(true);

    for (int i = 0; i < k_FramesAfterLifeEnds; ++i)
    {
        scene.OnUpdate();
    }

    EXPECT_TRUE(scene.Effects()->Exists(handle));
}

// 描画まで繋がったかは画素で見る。EffectScene::Draw の呼び出しを消すと落ちる
TEST_F(SceneRendererEffectTest, RenderDrawsThePlayedEffect)
{
    GameObject host;
    CameraComponent* camera = host.AddComponent<CameraComponent>();
    CameraBrain* brain = host.AddComponent<CameraBrain>();
    host.OnStart();

    std::unique_ptr<RenderTarget> target = RenderTarget::Create(NS::Core::Size2D{k_WindowSize, k_WindowSize});
    ASSERT_TRUE(target != nullptr);
    ASSERT_TRUE(target->IsValid());

    SceneRenderer renderer;
    renderer.SetEffectRoot(TestEffectRoot());
    renderer.SetRenderer(m_renderer.get());
    ASSERT_NE(renderer.Effects(), nullptr);
    ASSERT_TRUE(renderer.Effects()->Preload("square_r"));

    // 既定の CameraPose は原点を z=-5 から見る。原点で再生した板が中央に写る
    SceneView view{};
    view.target = target.get();
    view.viewPose = CameraPose{};
    renderer.SetSceneViews(std::vector<SceneView>{view});

    const SceneEnvironment environment{};
    renderer.Render(*brain, *camera, environment);
    const std::array<std::uint8_t, 4> before = ReadPixel(*target, k_Center);

    const EffectHandle handle = renderer.Effects()->Play("square_r");
    ASSERT_TRUE(handle.IsValid());
    renderer.UpdateEffects(k_Frame);

    renderer.Render(*brain, *camera, environment);
    const std::array<std::uint8_t, 4> after = ReadPixel(*target, k_Center);

    EXPECT_GT(LargestChannelDifference(before, after), k_VisibleDifference);

    m_renderer->SetSceneTarget(nullptr);
}
