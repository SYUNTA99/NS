#include "Editor/EditorMode.h"
#include "Editor/LevelFilePaths.h"
#include "Editor/Undo/CompositeCommand.h"
#include "Game/Game.h"
#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/StringUtils.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    // レベルの名前は Assets の下に限られるので、Assets の下に試しごとのフォルダを作り、終わったら消す
    class LevelOpenTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            m_folder = "EditorLevelOpenTest" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            m_directory = NS::OS::FileSystem::Combine(
                NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), m_folder);
            ASSERT_TRUE(NS::OS::FileSystem::CreateDirectories(m_directory));
        }

        void TearDown() override
        {
            std::error_code error;
            std::filesystem::remove_all(std::filesystem::path{NS::OS::StringUtils::WideFromUtf8(m_directory)}, error);
        }

        //! フォルダへ文字列のままファイルを書き、そのレベルの名前を返す
        std::string WriteLevel(std::string_view fileName, std::string_view contents)
        {
            const std::string path = NS::OS::FileSystem::Combine(m_directory, std::string{fileName} + ".scene");
            const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(contents.data()),
                                                   contents.size()};
            EXPECT_TRUE(NS::OS::FileSystem::WriteAllBytes(path, bytes));
            return m_folder + "/" + std::string{fileName};
        }

        std::string m_folder;
        std::string m_directory;
    };

    int CountObjectsOfClass(const nlohmann::json& scene, std::string_view className)
    {
        int count = 0;
        for (const nlohmann::json& object : NS::SceneJsonObjects(scene))
        {
            if (NS::ActorFileLibrary::Get().ClassNameOf(NS::ObjectJsonActorFile(object)) == className)
            {
                ++count;
            }
        }
        return count;
    }
} // namespace

// 起動シーンの道の持ち主は Game だけ。エディタのレベルの名前はそこから引く
TEST(EditorLevelOpen, StartSceneLevelNameComesFromTheGameStartScenePath)
{
    EXPECT_EQ(NS::Editor::LevelNameOfScenePath(::Game::StartScenePath()), "Scenes/new_scene");
}

// 名前の無いまま終了の保存をすると、起動シーンへ書き戻す。書き先の名前も Game の道から引く
TEST(EditorLevelOpen, QuitSaveWithoutALevelNameTargetsTheStartScene)
{
    NS::Editor::EditorMode editor;

    EXPECT_EQ(editor.QuitSaveLevelName(), NS::Editor::LevelNameOfScenePath(::Game::StartScenePath()));
}

// Assets の外・拡張子の違い・使えない文字は名前にしない
TEST(EditorLevelOpen, LevelNameOfScenePathRejectsPathsOutsideAssetsOrNotScenes)
{
    EXPECT_EQ(NS::Editor::LevelNameOfScenePath("Assets/Scenes/a.SCENE"), "Scenes/a");
    EXPECT_EQ(NS::Editor::LevelNameOfScenePath("Scenes/a.scene"), "");
    EXPECT_EQ(NS::Editor::LevelNameOfScenePath("Assets/Scenes/a.mat"), "");
    EXPECT_EQ(NS::Editor::LevelNameOfScenePath("Assets/Scenes/a"), "");
    EXPECT_EQ(NS::Editor::LevelNameOfScenePath("Assets/../a.scene"), "");
}

// 自機の居ないレベルを開くと、自機と落下死の範囲を 1 体ずつ補い、名前を控え、前の履歴を捨てて未保存でない
TEST_F(LevelOpenTest, OpenLevelFillsPlayerAndDeathZoneAndStartsCleanHistory)
{
    const std::string name = WriteLevel("empty", NS::MakeSceneJson().dump());

    NS::Editor::EditorMode editor;
    nlohmann::json loaded;
    editor.SetLoadLevelFn([&loaded](nlohmann::json&& fresh) { loaded = std::move(fresh); });
    // 開く前の編集が履歴に残っている状態にする
    editor.Undo().Record(
        std::make_unique<NS::Editor::CompositeCommand>(std::vector<std::unique_ptr<NS::Editor::ICommand>>{}));
    ASSERT_TRUE(editor.HasUnsavedChanges());

    ASSERT_TRUE(editor.OpenLevel(name));

    EXPECT_EQ(CountObjectsOfClass(loaded, "PlayerActor"), 1);
    EXPECT_EQ(CountObjectsOfClass(loaded, "DeathZoneActor"), 1);
    EXPECT_EQ(editor.CurrentLevelName(), name);
    EXPECT_EQ(editor.Undo().UndoSize(), 0u);
    EXPECT_FALSE(editor.HasUnsavedChanges());
}

// 実在するのに読めないレベルを名前の無いまま開くと失敗し、終了の保存は元のファイルを潰さず退避の名前へ書く
TEST_F(LevelOpenTest, OpenLevelOfABrokenFileFailsAndQuitSaveGoesToTheRecoveryName)
{
    const std::string name = WriteLevel("broken", "{ broken");

    NS::Editor::EditorMode editor;
    bool loadCalled = false;
    editor.SetLoadLevelFn([&loadCalled](nlohmann::json&&) { loadCalled = true; });

    EXPECT_FALSE(editor.OpenLevel(name));

    EXPECT_FALSE(loadCalled);
    EXPECT_TRUE(editor.CurrentLevelName().empty());
    EXPECT_EQ(editor.QuitSaveLevelName(), "Scenes/recovered_level");
}
