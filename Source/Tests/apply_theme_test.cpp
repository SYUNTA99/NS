#include "Editor/LevelEditorController.h"
#include "Framework/Core/Filesystem.h"
#include "Framework/Core/Logger.h"
#include "Game/Level/LevelData.h"
#include "Game/LevelPlayScene.h"
#include "Game/Theme/ThemeData.h"
#include "Game/Theme/ThemeId.h"
#include "Game/Theme/ThemeRegistry.h"

#include <filesystem>

#include <gtest/gtest.h>

namespace
{
    /// 雛形の値が要るため同梱テーマを読み込んだ状態で ApplyTheme を検証する
    /// Setup / OnStart は Application::Get() を要求するため呼ばない
    class ApplyThemeTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            NS::Core::Logger::Init();
            NS::Game::Theme::LoadThemesFromDirectory(NS::Core::FileSystem::ContentRoot() / "Assets" / "Themes");
        }
        void TearDown() override
        {
            // 次のテストが読込済み状態を仮定しないよう、存在しないディレクトリを読ませて中立へ戻す
            NS::Game::Theme::LoadThemesFromDirectory(std::filesystem::temp_directory_path() /
                                                         "ns_apply_theme_restore");
            NS::Core::Logger::Shutdown();
        }
    };

    TEST_F(ApplyThemeTest, CopiesTemplateIntoSceneEnvironment)
    {
        LevelPlayScene scene;
        LevelEditorController editor(&scene);

        editor.ApplyTheme(NS::Game::Theme::ThemeId::Lava);

        const NS::Game::Theme::ThemeData& lava = NS::Game::Theme::Get(NS::Game::Theme::ThemeId::Lava);
        const NS::Game::Level::LevelEnvironment& env = scene.Level().environment;
        EXPECT_EQ(env.skyboxCubemapPath, lava.skyboxCubemapPath.generic_string());
        EXPECT_FLOAT_EQ(env.lightDirection.x, lava.lightDirection.x);
        EXPECT_FLOAT_EQ(env.lightDirection.y, lava.lightDirection.y);
        EXPECT_FLOAT_EQ(env.lightDirection.z, lava.lightDirection.z);
        EXPECT_FLOAT_EQ(env.lightColor.x, lava.lightColor.x);
        EXPECT_FLOAT_EQ(env.ambientColor.z, lava.ambientColor.z);
    }
} // namespace
