#include "Framework/Scene/SceneBase.h"
#include "Framework/Scene/SceneManager.h"
#include "Game/Level/PlayFlowComponent.h"
#include "Game/LevelPlayScene.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

/// 既存型へ手を入れず SceneBase 派生を足すだけで、 SceneManager の差し替えに乗って動くことを検証する
/// LevelPlayScene は Application 不在でもライフサイクルが安全に戻るため、 実シーンとの相互差し替えも回す

namespace
{
    /// タイトル画面に相当する新シーン種別の代役。 ライフサイクルの呼出だけ記録する
    class DummyTitleScene : public NS::Scene::SceneBase
    {
    public:
        explicit DummyTitleScene(std::string* log) : m_log(log) {}

        void OnStart() override { m_log->append("Start;"); }
        void OnUpdate() override { m_log->append("Update;"); }
        void OnShutdown() override { m_log->append("Shutdown;"); }

    private:
        std::string* m_log;
    };
} // namespace

TEST(SceneSwap, NewSceneTypeRunsThroughManagerAlone)
{
    NS::Scene::SceneManager manager;
    std::string log;

    manager.LoadScene(std::make_unique<DummyTitleScene>(&log));
    manager.Update();

    EXPECT_EQ(log, "Start;Update;");
}

TEST(SceneSwap, LevelPlaySceneSwapsIntoNewSceneType)
{
    NS::Scene::SceneManager manager;
    manager.LoadScene(std::make_unique<LevelPlayScene>());
    manager.Update();

    std::string log;
    auto title = std::make_unique<DummyTitleScene>(&log);
    auto* titlePtr = title.get();
    manager.LoadScene(std::move(title));
    manager.Update();

    EXPECT_EQ(log, "Start;Update;");
    // 実行時型情報は切っているため、載せた実体そのものが現役かを識別で確かめる
    EXPECT_EQ(manager.Current(), titlePtr);
}

TEST(SceneSwap, SwapBackReturnsWorkingLevelPlayScene)
{
    NS::Scene::SceneManager manager;
    std::string log;
    manager.LoadScene(std::make_unique<DummyTitleScene>(&log));

    auto play = std::make_unique<LevelPlayScene>();
    auto* scene = play.get();
    manager.LoadScene(std::move(play));

    EXPECT_EQ(log, "Start;Shutdown;");
    ASSERT_EQ(manager.Current(), scene);

    // 差し替え後の実シーンが器として生きていることをプレイ突入で確かめる
    scene->Director().Flow().EnterPlay();
    EXPECT_TRUE(scene->Director().Flow().PlayModeSub().IsActive());
}
