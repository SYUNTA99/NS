#include <gtest/gtest.h>

#include "Editor/LevelFilePaths.h"
#include "Game/Game.h"
#include "Game/Level/BlockObject.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Scene/SceneData.h"
#include "Runtime/Object/Scene/SceneJson.h"
#include "Runtime/Platform/Filesystem.h"

#include <filesystem>
#include <optional>
#include <string>

namespace
{
    using NS::Platform::FileSystem;

    // 弾かれたかどうかは、読めるシーンを渡さないと分からない
    [[nodiscard]] std::optional<std::string> WriteTwoCellScene(const std::string& name)
    {
        const std::optional<std::string> path = NS::Editor::BuildLevelPath("TestOutput/" + name);
        if (!path.has_value())
        {
            return std::nullopt;
        }

        NS::Obj::SceneData data;
        data.objects.push_back(NS::Game::Level::MakeCellObject(0, 0, 0));
        data.objects.push_back(NS::Game::Level::MakeCellObject(1, 0, 0));
        NS::Obj::EnsureUniqueObjectIds(data);
        if (!NS::Obj::SaveSceneToJsonFile(data, *path))
        {
            return std::nullopt;
        }
        return path;
    }
} // namespace

// m_startSceneLoaded を true で始めると読めていないシーンを読めた事にするので、初期値を見る
TEST(NsGameStartScene, DoesNotClaimTheStartSceneIsLoadedBeforeReadingIt)
{
    const Game game;

    EXPECT_FALSE(game.StartSceneLoaded());
}

// 既定の開始シーンが読めないと Game.exe が空の世界で立ち上がる
TEST(NsGameStartScene, ReadsTheDefaultStartScene)
{
    Game game;

    EXPECT_TRUE(game.LoadStartScene());
}

TEST(NsGameStartScene, ReadsTheSceneItWasGivenAtConstruction)
{
    const std::optional<std::string> path = WriteTwoCellScene("start_scene_given");
    ASSERT_TRUE(path.has_value());

    Game game{std::filesystem::path{*path}.lexically_relative(FileSystem::ContentRoot()).generic_string()};

    ASSERT_TRUE(game.LoadStartScene());
    EXPECT_TRUE(game.StartSceneLoaded());
    ASSERT_NE(game.CurrentScene(), nullptr);
    // シーンのデータに無いカメラが 1 体加わるので、データの 2 個に 1 を足した数と比べる
    EXPECT_EQ(game.CurrentScene()->Objects().ObjectCount(), 3u);
}

// OnAttach は読めなくても空のシーンで立ち上がる。読めなかった事実は StartSceneLoaded にしか残らない
TEST(NsGameStartScene, KeepsTheStartSceneFailureVisible)
{
    Game game{"Assets/Scenes/no_such_scene.scene"};

    EXPECT_FALSE(game.LoadStartScene());
    EXPECT_FALSE(game.StartSceneLoaded());
}

// 絶対パスは ContentRoot と結合してもそのまま残る。中を指していれば配下の確認を通るので、入口で弾かないと読める
TEST(NsGameStartScene, RejectsAnAbsoluteStartScenePath)
{
    const std::optional<std::string> path = WriteTwoCellScene("start_scene_absolute");
    ASSERT_TRUE(path.has_value());

    Game game{*path};

    EXPECT_FALSE(game.LoadStartScene());
    EXPECT_EQ(game.CurrentScene(), nullptr);
}

// 空のパスは ResolveUnder が ContentRoot 自体を返すので、フォルダを読もうとする
TEST(NsGameStartScene, RejectsAnEmptyStartScenePath)
{
    Game game{""};

    EXPECT_FALSE(game.LoadStartScene());
}
