#include <gtest/gtest.h>

#include <ns/app/application.h>
#include <ns/app/scene.h>
#include <ns/core/logger.h>
#include <ns/graphics/renderer.h>
#include <ns/platform/input.h>
#include <ns/platform/window.h>

#include <memory>
#include <utility>

namespace
{
    using ns::app::Application;
    using ns::app::ApplicationDesc;
    using ns::app::Scene;

    ApplicationDesc MakeDesc(const char* title, int width = 320, int height = 240)
    {
        ApplicationDesc d{};
        d.window.title = title;
        d.window.width = width;
        d.window.height = height;
        d.window.visible = false;
#ifdef NS_BUILD_DEBUG
        d.renderer.enableDebugLayer = true;
#else
        d.renderer.enableDebugLayer = false;
#endif
        d.renderer.vsync = false;
        // テスト時間内で確実に複数 fixed step が発生するよう極小化 (default 1/60 だと
        // Debug layer ON の重い 1 frame 内に 1 step も入らないケースがある)。
        d.fixedDelta = 0.0005f;
        return d;
    }

    /// Scene 寿命は Application::Shutdown() で reset() されるため、
    /// 検証用カウンタは外部に置いて Scene 破棄後もアクセス可能にする。
    struct SceneCounters
    {
        int startCount = 0;
        int updateCount = 0;
        int renderCount = 0;
        int shutdownCount = 0;
        float lastAlpha = -1.0f;
    };

    /// 指定回数の OnUpdate 後に Application::Quit() を呼ぶ Scene。
    class QuittingScene : public Scene
    {
    public:
        int targetUpdates;
        SceneCounters* counters;

        QuittingScene(int target, SceneCounters* c) : targetUpdates(target), counters(c) {}

        void OnStart() override { ++counters->startCount; }
        void OnUpdate(float) override
        {
            ++counters->updateCount;
            if (counters->updateCount >= targetUpdates)
                Application::Quit();
        }
        void OnRender() override { ++counters->renderCount; }
        void OnShutdown() override { ++counters->shutdownCount; }
    };

    /// OnRender 中の Application::Alpha() を記録し、一定回数で Quit。
    class AlphaCheckScene : public Scene
    {
    public:
        SceneCounters* counters;

        explicit AlphaCheckScene(SceneCounters* c) : counters(c) {}

        void OnUpdate(float) override
        {
            if (counters->renderCount >= 2)
                Application::Quit();
        }
        void OnRender() override
        {
            counters->lastAlpha = Application::Alpha();
            ++counters->renderCount;
        }
    };
} // namespace

class ApplicationLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { ns::core::Logger::Init(); }
    void TearDown() override { ns::core::Logger::Shutdown(); }
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

TEST_F(ApplicationLoggerTest, RunWithNullSceneReturnsMinusOne)
{
    Application app(MakeDesc("ns_app_null_scene"));
    ASSERT_TRUE(app.IsValid());
    EXPECT_EQ(app.Run(nullptr), -1);
}

TEST_F(ApplicationLoggerTest, QuitTerminatesMainLoop)
{
    Application app(MakeDesc("ns_app_quit"));
    ASSERT_TRUE(app.IsValid());

    SceneCounters counters;
    auto scene = std::make_unique<QuittingScene>(3, &counters);
    const int code = app.Run(std::move(scene));

    EXPECT_EQ(code, 0);
    EXPECT_EQ(counters.startCount, 1);
    EXPECT_EQ(counters.shutdownCount, 1);
    EXPECT_GE(counters.updateCount, 3);
}

TEST_F(ApplicationLoggerTest, StaticAccessorsAreNullWhenNoInstance)
{
    EXPECT_EQ(Application::Get(), nullptr);
    EXPECT_FLOAT_EQ(Application::DeltaTime(), 0.0f);
    EXPECT_DOUBLE_EQ(Application::Time(), 0.0);
    EXPECT_FLOAT_EQ(Application::Alpha(), 0.0f);
    Application::Quit();
    SUCCEED();
}

TEST_F(ApplicationLoggerTest, AlphaIsInRangeDuringRender)
{
    Application app(MakeDesc("ns_app_alpha"));
    ASSERT_TRUE(app.IsValid());

    SceneCounters counters;
    auto scene = std::make_unique<AlphaCheckScene>(&counters);
    const int code = app.Run(std::move(scene));

    EXPECT_EQ(code, 0);
    EXPECT_GE(counters.lastAlpha, 0.0f);
    EXPECT_LT(counters.lastAlpha, 1.0f);
}
