#include "Game/Level/BlockObject.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Scene/SceneData.h"
#include "Runtime/Object/Scene/SceneManager.h"
#include "Runtime/Object/World.h"

#include <cstdint>
#include <gtest/gtest.h>

/// レベルの切り替えがシーンデータの差し替えだけで済むことを検証する
/// 差し替えた先の scene が動くことまで見る

namespace
{
    // 地形ブロックを指定数だけ並べたレベルデータ
    NS::Object::SceneData MakeLevel(std::int16_t cellCount)
    {
        NS::Object::SceneData data;
        for (std::int16_t i = 0; i < cellCount; ++i)
        {
            data.objects.push_back(NS::Game::Level::MakeCellObject(i, 0, 0));
        }
        return data;
    }
} // namespace

TEST(SceneSwap, SwapReplacesWorldContents)
{
    NS::Object::SceneManager manager;
    manager.LoadScene(MakeLevel(3));

    NS::Object::Scene& swapped = manager.LoadScene(MakeLevel(1));

    EXPECT_EQ(&swapped, manager.Current());
    // データの分 + world に常駐するカメラ 1 体
    EXPECT_EQ(swapped.World().ObjectCount(), 2u);
}

TEST(SceneSwap, SwappedSceneKeepsRunning)
{
    NS::Object::SceneManager manager;
    manager.LoadScene(MakeLevel(1));

    NS::Object::Scene& swapped = manager.LoadScene(MakeLevel(2));

    // 差し替え後の scene が動くことを、世界を 1 tick 回して確かめる
    EXPECT_TRUE(swapped.IsSimulationEnabled());
    (void)swapped.BeginPlayBaseline();
    manager.Update();

    EXPECT_EQ(swapped.World().ObjectCount(), 3u);
}

TEST(SceneSwap, SwapAfterUnloadStartsFresh)
{
    NS::Object::SceneManager manager;
    manager.LoadScene(MakeLevel(3));
    manager.UnloadScene();

    NS::Object::Scene& reloaded = manager.LoadScene(MakeLevel(1));
    EXPECT_EQ(reloaded.World().ObjectCount(), 2u);
}
