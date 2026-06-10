#include <gtest/gtest.h>

#include <Framework/Scene/SceneBase.h>
#include <Framework/Scene/SceneManager.h>

#include <memory>
#include <string>

namespace
{
    using NS::Scene::SceneBase;
    using NS::Scene::SceneManager;

    /// lifecycle 呼出回数を観測する Scene
    class TrackingScene : public SceneBase
    {
    public:
        TrackingScene(std::string name, std::string* log) : m_name(std::move(name)), m_log(log) {}

        void OnStart() override { m_log->append(m_name + ":Start;"); }
        void OnUpdate() override { m_log->append(m_name + ":Update;"); }
        void OnRenderScene() override { m_log->append(m_name + ":Render;"); }
        void OnShutdown() override { m_log->append(m_name + ":Shutdown;"); }

        const std::string& Name() const noexcept { return m_name; }

    private:
        std::string m_name;
        std::string* m_log;
    };
} // namespace

TEST(NsSceneManager, EmptyOnConstruction)
{
    SceneManager mgr;
    EXPECT_FALSE(mgr.HasScene());
    EXPECT_EQ(mgr.Current(), nullptr);
}

TEST(NsSceneManager, LoadSceneTriggersOnStartAndSetsCurrent)
{
    SceneManager mgr;
    std::string log;
    mgr.LoadScene(std::make_unique<TrackingScene>("A", &log));

    EXPECT_EQ(log, "A:Start;");
    EXPECT_TRUE(mgr.HasScene());
    ASSERT_NE(mgr.Current(), nullptr);
}

TEST(NsSceneManager, UpdateAndRenderForwardToCurrent)
{
    SceneManager mgr;
    std::string log;
    mgr.LoadScene(std::make_unique<TrackingScene>("A", &log));
    log.clear();

    mgr.Update();
    mgr.Render();
    EXPECT_EQ(log, "A:Update;A:Render;");
}

TEST(NsSceneManager, LoadSceneReplacesShuttingDownPrevious)
{
    SceneManager mgr;
    std::string log;
    mgr.LoadScene(std::make_unique<TrackingScene>("A", &log));
    log.clear();

    mgr.LoadScene(std::make_unique<TrackingScene>("B", &log));
    EXPECT_EQ(log, "A:Shutdown;B:Start;");
    EXPECT_TRUE(mgr.HasScene());
}

TEST(NsSceneManager, LoadSceneNullShutsDownAndClearsCurrent)
{
    SceneManager mgr;
    std::string log;
    mgr.LoadScene(std::make_unique<TrackingScene>("A", &log));
    log.clear();

    mgr.LoadScene(nullptr);
    EXPECT_EQ(log, "A:Shutdown;");
    EXPECT_FALSE(mgr.HasScene());
    EXPECT_EQ(mgr.Current(), nullptr);
}

TEST(NsSceneManager, DestructorShutsDownCurrent)
{
    std::string log;
    {
        SceneManager mgr;
        mgr.LoadScene(std::make_unique<TrackingScene>("A", &log));
        log.clear();
    }
    EXPECT_EQ(log, "A:Shutdown;");
}

TEST(NsSceneManager, UpdateRenderWhenNoSceneIsNoOp)
{
    SceneManager mgr;
    mgr.Update();
    mgr.Render();
    SUCCEED();
}
