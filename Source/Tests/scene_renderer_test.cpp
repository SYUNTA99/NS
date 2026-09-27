#include "pixel_readback.h"

#include <Runtime/Core/AABB.h>
#include <Runtime/Core/Logger.h>
#include <Runtime/Graphics/EffectScene.h>
#include <Runtime/Graphics/RenderContext.h>
#include <Runtime/Graphics/RenderTarget.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Object/Component.h>
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

    // 描く時に渡された補間の割合を控える。どこに置いても視錐台に入る
    class AlphaRecorder : public IRenderable
    {
    public:
        void Collect(const RenderContext& context, std::vector<NS::Gfx::DrawItem>&) override
        {
            alphas.push_back(context.alpha);
        }
        [[nodiscard]] NS::Core::AABB WorldBounds() const noexcept override
        {
            return NS::Core::AABB{NS::Core::Vector3{-1000.0f, -1000.0f, -1000.0f},
                                  NS::Core::Vector3{1000.0f, 1000.0f, 1000.0f}};
        }

        std::vector<float> alphas;
    };

    // 不透明の描画の途中で、1 を超える色の画素を 1 つ今の描画先へ書く
    // どこに置いても視錐台に入る
    class BrightPixelWriter : public IRenderable
    {
    public:
        void Collect(const RenderContext& context, std::vector<NS::Gfx::DrawItem>&) override
        {
            context.renderer->DrawScreenRect(static_cast<float>(k_Center.column),
                                             static_cast<float>(k_Center.row),
                                             1.0f,
                                             1.0f,
                                             NS::Core::Color{4.0f, 4.0f, 4.0f, 1.0f});
        }
        [[nodiscard]] NS::Core::AABB WorldBounds() const noexcept override
        {
            return NS::Core::AABB{NS::Core::Vector3{-1000.0f, -1000.0f, -1000.0f},
                                  NS::Core::Vector3{1000.0f, 1000.0f, 1000.0f}};
        }
    };

    // 最初の OnUpdate で mover_x を原点に 1 回だけ出す
    class PlayOnFirstUpdate : public NS::Obj::Component
    {
    public:
        void OnUpdate() override
        {
            if (m_played)
            {
                return;
            }
            m_played = true;
            NS::Obj::Scene* scene = Owner()->OwningScene();
            if (scene == nullptr || scene->Effects() == nullptr)
            {
                return;
            }
            handle = scene->Effects()->Play("mover_x", NS::Gfx::EffectPlayDesc{});
        }

        EffectHandle handle{};

    private:
        bool m_played = false;
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

    const std::string_view skyboxPath{}; // 空は skybox を描かない
    const float alpha = 1.0f;            // 視点を渡すので補間は写らない
    renderer.Render(*brain, *camera, skyboxPath, alpha);
    const std::array<std::uint8_t, 4> before = ReadPixel(*target, k_Center);

    const EffectHandle handle = renderer.Effects()->Play("square_r");
    ASSERT_TRUE(handle.IsValid());
    renderer.UpdateEffects(k_Frame);

    renderer.Render(*brain, *camera, skyboxPath, alpha);
    const std::array<std::uint8_t, 4> after = ReadPixel(*target, k_Center);

    EXPECT_GT(LargestChannelDifference(before, after), k_VisibleDifference);

    m_renderer->SetSceneTarget(nullptr);
}

// mover_x は生まれた所から +X へ 1 フレーム 0.5 m 進む縦横 0.4 m の板
// 固定ステップの Update 帯で出した層は、同じステップの終わりの UpdateEffects で生まれ、そのステップの絵に
// 生まれた瞬間の姿で写る。次のステップまで遅れる実装だと中央が描かれず、2 回進める実装だと 0.5 m 先に写る
TEST_F(SceneRendererEffectTest, EffectPlayedInAStepIsDrawnAtItsBirthInThatStepsPicture)
{
    Scene scene;
    scene.SetEffectRoot(TestEffectRoot());
    scene.SetRenderer(m_renderer.get());
    ASSERT_NE(scene.Effects(), nullptr);
    ASSERT_TRUE(scene.Effects()->Preload("mover_x"));

    std::unique_ptr<GameObject> object = std::make_unique<GameObject>();
    PlayOnFirstUpdate* player = object->AddComponent<PlayOnFirstUpdate>();
    ASSERT_NE(scene.SpawnObject(std::move(object), "Player"), nullptr);

    std::unique_ptr<RenderTarget> target = RenderTarget::Create(NS::Core::Size2D{k_WindowSize, k_WindowSize});
    ASSERT_TRUE(target != nullptr);
    ASSERT_TRUE(target->IsValid());
    SceneView view{};
    view.target = target.get();
    view.viewPose = CameraPose{};
    scene.SetSceneViews(std::vector<SceneView>{view});

    scene.OnRender();
    const std::array<std::uint8_t, 4> background = ReadPixel(*target, k_Center);

    scene.OnUpdate();
    scene.OnRender();
    ASSERT_TRUE(player->handle.IsValid());

    // 既定の視点は 5 m 先を縦 60 度で見るので、1 m が約 11 画素。0.5 m 先は右へ 6 画素
    const PixelPosition oneStepAhead{k_Center.column + 6, k_Center.row};
    EXPECT_GT(LargestChannelDifference(background, ReadPixel(*target, k_Center)), k_VisibleDifference);
    EXPECT_LE(LargestChannelDifference(background, ReadPixel(*target, oneStepAhead)), k_VisibleDifference);

    m_renderer->SetSceneTarget(nullptr);
}

// 世界は 1 を超える明るさを持てる描画先へ描かれる
// 超えた分が周りへにじんで、今の描画先へ書き戻される
TEST_F(SceneRendererEffectTest, WorldBrighterThanOneBleedsIntoItsSurroundings)
{
    GameObject host;
    CameraComponent* camera = host.AddComponent<CameraComponent>();
    CameraBrain* brain = host.AddComponent<CameraBrain>();
    host.OnStart();

    std::unique_ptr<RenderTarget> target = RenderTarget::Create(NS::Core::Size2D{k_WindowSize, k_WindowSize});
    ASSERT_TRUE(target != nullptr);
    ASSERT_TRUE(target->IsValid());

    SceneRenderer renderer;
    renderer.SetRenderer(m_renderer.get());
    SceneView view{};
    view.target = target.get();
    view.viewPose = CameraPose{};
    renderer.SetSceneViews(std::vector<SceneView>{view});

    const std::string_view skyboxPath{}; // 空は skybox を描かない
    const float alpha = 1.0f;            // 視点を渡すので補間は写らない
    const PixelPosition beside{k_Center.column + 1, k_Center.row};
    renderer.Render(*brain, *camera, skyboxPath, alpha);
    const std::array<std::uint8_t, 4> background = ReadPixel(*target, beside);

    BrightPixelWriter writer;
    renderer.RegisterRenderable(&writer);
    renderer.SyncRenderBounds();
    renderer.Render(*brain, *camera, skyboxPath, alpha);
    const std::array<std::uint8_t, 4> bled = ReadPixel(*target, beside);

    // 隣の画素は Bloom の既定の設定で背景より 6 明るくなる。8 ビットの丸めの揺れ (1) の 3
    // 倍を超えれば、にじんだと言える
    EXPECT_GT(LargestChannelDifference(background, bled), 3);

    renderer.UnregisterRenderable(&writer);
    m_renderer->SetSceneTarget(nullptr);
}

// 止めて 1 フレームずつ進める間は、描く度に割合が実時間で変わると
// 同じフレームの絵が揺れる
TEST_F(SceneRendererEffectTest, PausedSceneRendersTheLatestStepWithoutInterpolating)
{
    Scene scene;
    scene.SetRenderer(m_renderer.get());
    AlphaRecorder recorder;
    scene.RegisterRenderable(&recorder);

    std::unique_ptr<RenderTarget> target = RenderTarget::Create(NS::Core::Size2D{k_WindowSize, k_WindowSize});
    ASSERT_TRUE(target != nullptr);
    SceneView view{};
    view.target = target.get();
    scene.SetSceneViews(std::vector<SceneView>{view});

    // 積み残しの時間を 0 にし、実時間の割合を 0 に置く
    NS::Platform::FrameTimer::Reset();
    scene.SetSimulationPaused(true);
    scene.StepSimulation();
    scene.OnUpdate();
    scene.OnRender();

    ASSERT_EQ(recorder.alphas.size(), 1u);
    EXPECT_FLOAT_EQ(recorder.alphas[0], 1.0f);

    scene.UnregisterRenderable(&recorder);
    m_renderer->SetSceneTarget(nullptr);
}

// 上の試しは割合を常に 1 にする実装でも通る
// 回っている間は実時間の割合のまま描くことを別に見る
TEST_F(SceneRendererEffectTest, RunningSceneRendersWithTheFrameTimerAlpha)
{
    Scene scene;
    scene.SetRenderer(m_renderer.get());
    AlphaRecorder recorder;
    scene.RegisterRenderable(&recorder);

    std::unique_ptr<RenderTarget> target = RenderTarget::Create(NS::Core::Size2D{k_WindowSize, k_WindowSize});
    ASSERT_TRUE(target != nullptr);
    SceneView view{};
    view.target = target.get();
    scene.SetSceneViews(std::vector<SceneView>{view});

    NS::Platform::FrameTimer::Reset();
    scene.OnUpdate();
    scene.OnRender();

    ASSERT_EQ(recorder.alphas.size(), 1u);
    EXPECT_FLOAT_EQ(recorder.alphas[0], NS::Platform::FrameTimer::Alpha());
    EXPECT_LT(recorder.alphas[0], 1.0f);

    scene.UnregisterRenderable(&recorder);
    m_renderer->SetSceneTarget(nullptr);
}
