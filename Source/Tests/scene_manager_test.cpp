#include "Game/Level/BlockObject.h"

#include <Runtime/Object/AssetManager.h>
#include <Runtime/Object/Component.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Scene/SceneData.h>
#include <Runtime/Object/Scene/SceneManager.h>
#include <Runtime/Object/World.h>
#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <utility>

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
    // データの分 + world に常駐するカメラ 1 体
    EXPECT_EQ(scene.World().ObjectCount(), 4u);
}

TEST(NsSceneManager, LoadSceneReplacesPreviousWorld)
{
    SceneManager mgr;
    mgr.LoadScene(MakeLevel(3));

    NS::Object::Scene& second = mgr.LoadScene(MakeLevel(1));
    // 前の scene が破棄されていれば、残るのは新しいデータの分だけ
    EXPECT_EQ(second.World().ObjectCount(), 2u);
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
    EXPECT_EQ(mgr.Current()->World().ObjectCount(), 2u);
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

namespace
{
    // 引き当てと開始の順番を記録する検証用 Component
    class ResolveOrderProbeComponent : public NS::Object::Component
    {
    public:
        void ResolveAssets(NS::Object::AssetManager&) override
        {
            resolved = true;
            resolvedBeforeStart = !started;
        }
        void OnStart() override { started = true; }

        bool resolved = false;
        bool started = false;
        bool resolvedBeforeStart = false;
    };
} // namespace

// AssetManager 未設定でも SpawnTransient は落ちず、引き当てが走らない
TEST(NsSceneManager, SpawnTransientWithoutAssetsSkipsResolve)
{
    NS::Object::Scene scene;
    auto owned = std::make_unique<NS::Object::GameObject>();
    auto* probe = owned->AddComponent<ResolveOrderProbeComponent>();
    NS::Object::GameObject* spawned = scene.SpawnTransient(std::move(owned));

    ASSERT_NE(spawned, nullptr);
    EXPECT_TRUE(probe->started);
    EXPECT_FALSE(probe->resolved);
}

// 引き当ては OnStart より前。OnStart の中で資産を読む Component が空の参照を掴まない
TEST(NsSceneManager, SpawnTransientResolvesAssetsBeforeStart)
{
    NS::Object::AssetManager assets{std::filesystem::path{"."}};
    NS::Object::Scene scene;
    scene.SetAssets(&assets);
    auto owned = std::make_unique<NS::Object::GameObject>();
    auto* probe = owned->AddComponent<ResolveOrderProbeComponent>();
    NS::Object::GameObject* spawned = scene.SpawnTransient(std::move(owned));

    ASSERT_NE(spawned, nullptr);
    EXPECT_TRUE(probe->resolved);
    EXPECT_TRUE(probe->resolvedBeforeStart);
}
