#include "Game/Level/BlockObject.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Scene/SceneData.h>
#include <Runtime/Object/Scene/SceneManager.h>
#include <Runtime/Object/World.h>

namespace
{
    using NS::Object::SceneData;
    using NS::Object::SceneManager;

    // 地形ブロックを指定数だけ並べたレベルデータ
    SceneData MakeLevel(std::int16_t cellCount)
    {
        SceneData data;
        for (std::int16_t i = 0; i < cellCount; ++i)
        {
            data.objects.push_back(NS::Game::Level::MakeCellObject(i, 0, 0));
        }
        return data;
    }
} // namespace

TEST(NsSceneManager, EmptyOnConstruction)
{
    SceneManager mgr;
    EXPECT_FALSE(mgr.HasScene());
    EXPECT_EQ(mgr.Current(), nullptr);
}

TEST(NsSceneManager, LoadSceneBuildsWorldFromData)
{
    SceneManager mgr;
    NS::Object::Scene& scene = mgr.LoadScene(MakeLevel(3));

    EXPECT_TRUE(mgr.HasScene());
    EXPECT_EQ(mgr.Current(), &scene);
    EXPECT_EQ(scene.World().ObjectCount(), 3u);
}

TEST(NsSceneManager, LoadSceneReplacesPreviousWorld)
{
    SceneManager mgr;
    mgr.LoadScene(MakeLevel(3));

    NS::Object::Scene& second = mgr.LoadScene(MakeLevel(1));
    // 前の scene が畳まれていれば、残るのは新しいデータの分だけ
    EXPECT_EQ(second.World().ObjectCount(), 1u);
}

TEST(NsSceneManager, UnloadSceneClearsCurrent)
{
    SceneManager mgr;
    mgr.LoadScene(MakeLevel(1));

    mgr.UnloadScene();
    EXPECT_FALSE(mgr.HasScene());
    EXPECT_EQ(mgr.Current(), nullptr);
}

TEST(NsSceneManager, UpdateReachesLoadedScene)
{
    SceneManager mgr;
    mgr.LoadScene(MakeLevel(1));

    mgr.Update();
    EXPECT_EQ(mgr.Current()->World().ObjectCount(), 1u);
}

TEST(NsSceneManager, DestructorUnloadsCurrent)
{
    {
        SceneManager mgr;
        mgr.LoadScene(MakeLevel(1));
    }
    SUCCEED();
}

TEST(NsSceneManager, UpdateRenderWhenNoSceneIsNoOp)
{
    SceneManager mgr;
    mgr.Update();
    mgr.Render();
    SUCCEED();
}
