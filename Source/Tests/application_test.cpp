#include <gtest/gtest.h>
#include <memory>
#include <Runtime/App/Application.h>
#include <Runtime/App/Layer.h>
#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Logger.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Platform/Window.h>

namespace
{
    using NS::App::Application;
    using NS::App::ApplicationDesc;
    using NS::App::Layer;

    ApplicationDesc MakeDesc(const char* title, int width = 320, int height = 240)
    {
        ApplicationDesc d{};
        d.window.title = title;
        d.window.size = NS::Core::Size2D{width, height};
        d.window.visible = false;
#ifdef NS_BUILD_DEBUG
        d.renderer.enableDebugLayer = true;
#else
        d.renderer.enableDebugLayer = false;
#endif
        d.renderer.vsync = false;
        // 既定 (1/60) だと Debug layer 有効時の重い 1 frame に fixed step が入らないことがあるため、
        // テスト時間内で確実に複数回発生するよう極小化する
        d.fixedDelta = 0.0005f;
        return d;
    }

    //! Layer 寿命は Application::Shutdown() で reset() されるため、
    //! 検証用カウンタは外部に置いて Layer 破棄後もアクセス可能にする
    struct LayerCounters
    {
        int attachCount = 0;
        int updateCount = 0;
        int renderCount = 0;
        int detachCount = 0;
        float lastAlpha = -1.0f;
    };

    //! 指定回数の OnUpdate 後に Application::Quit() を呼ぶ Layer
    class QuittingLayer : public Layer
    {
    public:
        int targetUpdates;
        LayerCounters* counters;

        QuittingLayer(int target, LayerCounters* c) : Layer("QuittingLayer"), targetUpdates(target), counters(c) {}

        void OnAttach() override { ++counters->attachCount; }
        void OnUpdate() override
        {
            ++counters->updateCount;
            if (counters->updateCount >= targetUpdates)
                Application::Quit();
        }
        void OnRender() override { ++counters->renderCount; }
        void OnDetach() override { ++counters->detachCount; }
    };

    //! OnRender 中の Alpha を記録し、一定回数で Quit
    class AlphaCheckLayer : public Layer
    {
    public:
        LayerCounters* counters;

        explicit AlphaCheckLayer(LayerCounters* c) : Layer("AlphaCheckLayer"), counters(c) {}

        void OnUpdate() override
        {
            if (counters->renderCount >= 2)
                Application::Quit();
        }
        void OnRender() override
        {
            counters->lastAlpha = NS::Core::FrameTimer::Alpha();
            ++counters->renderCount;
        }
    };
} // namespace

class ApplicationLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST_F(ApplicationLoggerTest, ConstructsAndIsValid)
{
    Application app(MakeDesc("ns_app_construct"));
    EXPECT_TRUE(app.IsValid());
    EXPECT_EQ(Application::Get(), &app);
}

TEST_F(ApplicationLoggerTest, AccessorsReturnSubsystems)
{
    Application app(MakeDesc("ns_app_accessors"));
    ASSERT_TRUE(app.IsValid());
    EXPECT_TRUE(app.Window().IsValid());
    EXPECT_TRUE(app.Renderer().IsValid());
    (void)app.Input();
    SUCCEED();
}

TEST_F(ApplicationLoggerTest, GetIsNullBeforeAndAfterConstruction)
{
    EXPECT_EQ(Application::Get(), nullptr);
    {
        Application app(MakeDesc("ns_app_get_lifetime"));
        ASSERT_TRUE(app.IsValid());
        EXPECT_EQ(Application::Get(), &app);
    }
    EXPECT_EQ(Application::Get(), nullptr);
}

TEST_F(ApplicationLoggerTest, RunWithNoLayersReturnsMinusOne)
{
    Application app(MakeDesc("ns_app_no_layers"));
    ASSERT_TRUE(app.IsValid());
    EXPECT_EQ(app.Run(), -1);
}

TEST_F(ApplicationLoggerTest, QuitTerminatesMainLoop)
{
    Application app(MakeDesc("ns_app_quit"));
    ASSERT_TRUE(app.IsValid());

    LayerCounters counters;
    app.AddLayer(std::make_unique<QuittingLayer>(3, &counters));
    const int code = app.Run();

    EXPECT_EQ(code, 0);
    EXPECT_EQ(counters.attachCount, 1);
    EXPECT_EQ(counters.detachCount, 1);
    EXPECT_GE(counters.updateCount, 3);
}

TEST_F(ApplicationLoggerTest, StaticAccessorsAreSafeWithoutInstance)
{
    EXPECT_EQ(Application::Get(), nullptr);
    Application::Quit();
    SUCCEED();
}

TEST_F(ApplicationLoggerTest, AlphaIsInRangeDuringRender)
{
    Application app(MakeDesc("ns_app_alpha"));
    ASSERT_TRUE(app.IsValid());

    LayerCounters counters;
    app.AddLayer(std::make_unique<AlphaCheckLayer>(&counters));
    const int code = app.Run();

    EXPECT_EQ(code, 0);
    EXPECT_GE(counters.lastAlpha, 0.0f);
    EXPECT_LT(counters.lastAlpha, 1.0f);
}
