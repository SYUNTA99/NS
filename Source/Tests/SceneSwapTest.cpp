#include "Editor/EditorObjects.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Scene/SceneData.h"
#include "Runtime/Object/Scene/SceneManager.h"

#include <cstdint>
#include <gtest/gtest.h>

//! レベルの切り替えがシーンデータの差し替えだけで済むことを検証する
//! 差し替えた先の scene が動くことまで見る

namespace
{
    // 地形ブロックを指定数だけ並べたレベルデータ
    NS::Obj::SceneData MakeLevel(std::int16_t cellCount)
    {
        NS::Obj::SceneData data;
        for (std::int16_t i = 0; i < cellCount; ++i)
        {
            data.objects.push_back(NS::Editor::MakeCellObject(i, 0, 0));
        }
        return data;
    }
} // namespace

TEST(SceneSwap, SwapReplacesObjectListContents)
{
    NS::Obj::SceneManager manager;
    manager.LoadScene(MakeLevel(3));

    NS::Obj::Scene& swapped = manager.LoadScene(MakeLevel(1));

    EXPECT_EQ(&swapped, manager.Current());
    // データの分 + シーンに常駐するカメラ 1 体
    EXPECT_EQ(swapped.Objects().ObjectCount(), 2u);
}

TEST(SceneSwap, SwappedSceneKeepsRunning)
{
    NS::Obj::SceneManager manager;
    manager.LoadScene(MakeLevel(1));

    NS::Obj::Scene& swapped = manager.LoadScene(MakeLevel(2));

    // 差し替え後の scene が動くことを、1 tick 回して確かめる
    EXPECT_TRUE(swapped.IsSimulationEnabled());
    (void)swapped.BeginPlayBaseline();
    manager.Update();

    EXPECT_EQ(swapped.Objects().ObjectCount(), 3u);
}

TEST(SceneSwap, SwapAfterUnloadStartsFresh)
{
    NS::Obj::SceneManager manager;
    manager.LoadScene(MakeLevel(3));
    manager.UnloadScene();

    NS::Obj::Scene& reloaded = manager.LoadScene(MakeLevel(1));
    EXPECT_EQ(reloaded.Objects().ObjectCount(), 2u);
}
