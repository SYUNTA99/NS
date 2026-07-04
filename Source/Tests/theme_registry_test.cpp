#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <Framework/Core/Filesystem.h>
#include <Framework/Core/Logger.h>
#include <Game/Level/LevelData.h>
#include <Game/Theme/ThemeData.h>
#include <Game/Theme/ThemeId.h>
#include <Game/Theme/ThemeRegistry.h>

using namespace NS::Game::Theme;

namespace
{
    /// テスト用にユニークな一時ディレクトリパスを生成する
    std::filesystem::path MakeTempDir(const std::string& suffix)
    {
        const auto ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        return std::filesystem::temp_directory_path() / ("ns_theme_" + std::to_string(ns) + "_" + suffix);
    }

    void WriteTextFile(const std::filesystem::path& path, const std::string& text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path);
        out << text;
    }

    /// 全フィールドの一致を表明する。float は EXPECT_FLOAT_EQ で JSON 往復の丸めを許容する
    void ExpectThemeEq(const ThemeData& actual, const ThemeData& expected)
    {
        EXPECT_EQ(actual.displayName, expected.displayName);
        EXPECT_EQ(actual.blockTextureArrayBaseSlice, expected.blockTextureArrayBaseSlice);
        EXPECT_EQ(actual.skyboxCubemapPath, expected.skyboxCubemapPath);
        EXPECT_FLOAT_EQ(actual.lightDirection.x, expected.lightDirection.x);
        EXPECT_FLOAT_EQ(actual.lightDirection.y, expected.lightDirection.y);
        EXPECT_FLOAT_EQ(actual.lightDirection.z, expected.lightDirection.z);
        EXPECT_FLOAT_EQ(actual.lightColor.x, expected.lightColor.x);
        EXPECT_FLOAT_EQ(actual.lightColor.y, expected.lightColor.y);
        EXPECT_FLOAT_EQ(actual.lightColor.z, expected.lightColor.z);
        EXPECT_FLOAT_EQ(actual.ambientColor.x, expected.ambientColor.x);
        EXPECT_FLOAT_EQ(actual.ambientColor.y, expected.ambientColor.y);
        EXPECT_FLOAT_EQ(actual.ambientColor.z, expected.ambientColor.z);
    }

    TEST(ThemeRegistryTest, FiveThemesExist)
    {
        EXPECT_EQ(static_cast<int>(ThemeId::Count), 5);
    }

    TEST(ThemeRegistryTest, AllThemesDistinct)
    {
        const ThemeData& grass = Get(ThemeId::Grass);
        const ThemeData& cave = Get(ThemeId::Cave);
        const ThemeData& snow = Get(ThemeId::Snow);
        const ThemeData& lava = Get(ThemeId::Lava);
        const ThemeData& sky = Get(ThemeId::Sky);

        // 表示名は全て異なる文字列であること
        ASSERT_FALSE(grass.displayName.empty());
        ASSERT_FALSE(cave.displayName.empty());
        ASSERT_FALSE(snow.displayName.empty());
        ASSERT_FALSE(lava.displayName.empty());
        ASSERT_FALSE(sky.displayName.empty());

        EXPECT_NE(grass.displayName, cave.displayName);
        EXPECT_NE(grass.displayName, snow.displayName);
        EXPECT_NE(grass.displayName, lava.displayName);
        EXPECT_NE(grass.displayName, sky.displayName);
        EXPECT_NE(cave.displayName, snow.displayName);
        EXPECT_NE(cave.displayName, lava.displayName);
        EXPECT_NE(cave.displayName, sky.displayName);
        EXPECT_NE(snow.displayName, lava.displayName);
        EXPECT_NE(snow.displayName, sky.displayName);
        EXPECT_NE(lava.displayName, sky.displayName);

        // theme tint (lightColor or ambientColor) が片方でも違えば視覚差異が出る
        auto distinctTint = [](const ThemeData& a, const ThemeData& b) {
            const bool sameLight = a.lightColor.x == b.lightColor.x && a.lightColor.y == b.lightColor.y &&
                                   a.lightColor.z == b.lightColor.z;
            const bool sameAmb = a.ambientColor.x == b.ambientColor.x && a.ambientColor.y == b.ambientColor.y &&
                                 a.ambientColor.z == b.ambientColor.z;
            return !(sameLight && sameAmb);
        };
        EXPECT_TRUE(distinctTint(grass, cave));
        EXPECT_TRUE(distinctTint(grass, snow));
        EXPECT_TRUE(distinctTint(grass, lava));
        EXPECT_TRUE(distinctTint(grass, sky));
        EXPECT_TRUE(distinctTint(cave, snow));
        EXPECT_TRUE(distinctTint(cave, lava));
        EXPECT_TRUE(distinctTint(cave, sky));
        EXPECT_TRUE(distinctTint(snow, lava));
        EXPECT_TRUE(distinctTint(snow, sky));
        EXPECT_TRUE(distinctTint(lava, sky));
    }

    TEST(ThemeRegistryTest, OutOfRangeFallsBackToGrass)
    {
        const ThemeData& grass = Get(ThemeId::Grass);

        const ThemeData& byEnum = Get(static_cast<ThemeId>(99));
        const ThemeData& byUint16 = Get(static_cast<std::uint16_t>(999));

        EXPECT_EQ(&byEnum, &grass);
        EXPECT_EQ(&byUint16, &grass);
        EXPECT_EQ(byEnum.displayName, grass.displayName);
        EXPECT_EQ(byUint16.displayName, grass.displayName);
    }

    TEST(ThemeRegistryTest, LevelDataThemeIdRoundTrip)
    {
        NS::Game::Level::LevelData level{};
        level.themeId = 3;
        const ThemeData& theme = Get(level.themeId);
        EXPECT_EQ(theme.displayName, "Lava");
    }

    /// ファイル読込系。TearDown で存在しないディレクトリを読ませ、全テーマを組み込み既定値へ戻す
    class ThemeRegistryFileTest : public ::testing::Test
    {
    protected:
        void SetUp() override { NS::Core::Logger::Init(); }
        void TearDown() override
        {
            LoadThemesFromDirectory(MakeTempDir("restore_missing"));
            NS::Core::Logger::Shutdown();
        }
    };

    TEST_F(ThemeRegistryFileTest, LoadAppliesFileValues)
    {
        const auto dir = MakeTempDir("apply");
        WriteTextFile(dir / "grass.theme", R"({
            "displayName": "Meadow",
            "blockTextureArrayBaseSlice": 4,
            "skyboxCubemapPath": "Assets/Skybox/other/",
            "lightDirection": [0.5, -1.0, 0.25],
            "lightColor": [0.5, 0.6, 0.7],
            "ambientColor": [0.05, 0.10, 0.15]
        })");

        LoadThemesFromDirectory(dir);

        const ThemeData& grass = Get(ThemeId::Grass);
        EXPECT_EQ(grass.displayName, "Meadow");
        EXPECT_EQ(grass.blockTextureArrayBaseSlice, 4);
        EXPECT_EQ(grass.skyboxCubemapPath, std::filesystem::path("Assets/Skybox/other/"));
        EXPECT_FLOAT_EQ(grass.lightDirection.x, 0.5f);
        EXPECT_FLOAT_EQ(grass.lightDirection.z, 0.25f);
        EXPECT_FLOAT_EQ(grass.lightColor.y, 0.6f);
        EXPECT_FLOAT_EQ(grass.ambientColor.z, 0.15f);

        std::filesystem::remove_all(dir);
    }

    TEST_F(ThemeRegistryFileTest, MissingFileKeepsBuiltinDefaults)
    {
        // 読込前 = 組み込み既定値の写しを取り、grass だけのディレクトリを読ませる
        const ThemeData builtinCave = Get(ThemeId::Cave);

        const auto dir = MakeTempDir("missing");
        WriteTextFile(dir / "grass.theme", R"({"displayName": "Meadow"})");

        LoadThemesFromDirectory(dir);

        ExpectThemeEq(Get(ThemeId::Cave), builtinCave);
        EXPECT_EQ(Get(ThemeId::Grass).displayName, "Meadow");

        std::filesystem::remove_all(dir);
    }

    TEST_F(ThemeRegistryFileTest, BrokenFileFallsBackToBuiltin)
    {
        const ThemeData builtinGrass = Get(ThemeId::Grass);

        const auto dir = MakeTempDir("broken");
        WriteTextFile(dir / "grass.theme", "{ this is not json ,,,");

        LoadThemesFromDirectory(dir);

        ExpectThemeEq(Get(ThemeId::Grass), builtinGrass);

        std::filesystem::remove_all(dir);
    }

    TEST_F(ThemeRegistryFileTest, PartialFileKeepsDefaultsForMissingKeys)
    {
        const ThemeData builtinGrass = Get(ThemeId::Grass);

        const auto dir = MakeTempDir("partial");
        WriteTextFile(dir / "grass.theme", R"({"lightColor": [0.1, 0.2, 0.3]})");

        LoadThemesFromDirectory(dir);

        const ThemeData& grass = Get(ThemeId::Grass);
        EXPECT_FLOAT_EQ(grass.lightColor.x, 0.1f);
        EXPECT_FLOAT_EQ(grass.lightColor.y, 0.2f);
        EXPECT_FLOAT_EQ(grass.lightColor.z, 0.3f);
        // 書かれていないキーは組み込み既定値のまま残る
        EXPECT_EQ(grass.displayName, builtinGrass.displayName);
        EXPECT_EQ(grass.blockTextureArrayBaseSlice, builtinGrass.blockTextureArrayBaseSlice);
        EXPECT_FLOAT_EQ(grass.lightDirection.x, builtinGrass.lightDirection.x);
        EXPECT_FLOAT_EQ(grass.ambientColor.x, builtinGrass.ambientColor.x);

        std::filesystem::remove_all(dir);
    }

    TEST_F(ThemeRegistryFileTest, UnknownKeysAreIgnored)
    {
        const auto dir = MakeTempDir("unknown");
        WriteTextFile(dir / "grass.theme", R"({
            "displayName": "Meadow",
            "futureKey": {"nested": 1},
            "anotherUnknown": [1, 2, 3]
        })");

        LoadThemesFromDirectory(dir);

        EXPECT_EQ(Get(ThemeId::Grass).displayName, "Meadow");

        std::filesystem::remove_all(dir);
    }

    TEST_F(ThemeRegistryFileTest, BundledThemeFilesMatchBuiltinDefaults)
    {
        // 同梱ファイルと組み込み既定値が同値なら、ファイル欠落時も見た目が変わらない保証になる
        const ThemeData builtin[5] = {
            Get(ThemeId::Grass), Get(ThemeId::Cave), Get(ThemeId::Snow), Get(ThemeId::Lava), Get(ThemeId::Sky)};

        LoadThemesFromDirectory(NS::Core::FileSystem::ContentRoot() / "Assets" / "Themes");

        ExpectThemeEq(Get(ThemeId::Grass), builtin[0]);
        ExpectThemeEq(Get(ThemeId::Cave), builtin[1]);
        ExpectThemeEq(Get(ThemeId::Snow), builtin[2]);
        ExpectThemeEq(Get(ThemeId::Lava), builtin[3]);
        ExpectThemeEq(Get(ThemeId::Sky), builtin[4]);
    }
} // namespace
