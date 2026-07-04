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

    std::filesystem::path BundledThemesDir()
    {
        return NS::Core::FileSystem::ContentRoot() / "Assets" / "Themes";
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

    TEST(ThemeRegistryTest, OutOfRangeFallsBackToGrass)
    {
        const ThemeData& grass = Get(ThemeId::Grass);

        const ThemeData& byEnum = Get(static_cast<ThemeId>(99));
        const ThemeData& byUint16 = Get(static_cast<std::uint16_t>(999));

        EXPECT_EQ(&byEnum, &grass);
        EXPECT_EQ(&byUint16, &grass);
    }

    /// 同梱の `.asset` を読み込んだ状態を検証する。値の出所はファイルが正
    class ThemeRegistryBundledTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            NS::Core::Logger::Init();
            LoadThemesFromDirectory(BundledThemesDir());
        }
        void TearDown() override
        {
            // 次のテストが読込済み状態を仮定しないよう中立へ戻す
            LoadThemesFromDirectory(MakeTempDir("restore_missing"));
            NS::Core::Logger::Shutdown();
        }
    };

    TEST_F(ThemeRegistryBundledTest, BundledThemesShipExpectedIdentity)
    {
        // 同梱ファイルの表示名と block slice 帯を固定する。band は RebuildWorld の焼き込みが依存する
        EXPECT_EQ(Get(ThemeId::Grass).displayName, "Grass");
        EXPECT_EQ(Get(ThemeId::Cave).displayName, "Cave");
        EXPECT_EQ(Get(ThemeId::Snow).displayName, "Snow");
        EXPECT_EQ(Get(ThemeId::Lava).displayName, "Lava");
        EXPECT_EQ(Get(ThemeId::Sky).displayName, "Sky");

        EXPECT_EQ(Get(ThemeId::Grass).blockTextureArrayBaseSlice, 0);
        EXPECT_EQ(Get(ThemeId::Cave).blockTextureArrayBaseSlice, 8);
        EXPECT_EQ(Get(ThemeId::Snow).blockTextureArrayBaseSlice, 16);
        EXPECT_EQ(Get(ThemeId::Lava).blockTextureArrayBaseSlice, 24);
        EXPECT_EQ(Get(ThemeId::Sky).blockTextureArrayBaseSlice, 32);
    }

    TEST_F(ThemeRegistryBundledTest, AllThemesDistinct)
    {
        const ThemeData& grass = Get(ThemeId::Grass);
        const ThemeData& cave = Get(ThemeId::Cave);
        const ThemeData& snow = Get(ThemeId::Snow);
        const ThemeData& lava = Get(ThemeId::Lava);
        const ThemeData& sky = Get(ThemeId::Sky);

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

    TEST_F(ThemeRegistryBundledTest, Uint16ThemeIdMapsToTheme)
    {
        // 旧形式の themeId 移行が使う uint16_t 版の取得。 番号がテーマへ正しく写る
        const ThemeData& theme = Get(static_cast<std::uint16_t>(3));
        EXPECT_EQ(theme.displayName, "Lava");
    }

    TEST_F(ThemeRegistryBundledTest, MakeEnvironmentFromThemeCopiesVisualFields)
    {
        const ThemeData& lava = Get(ThemeId::Lava);
        const NS::Game::Level::LevelEnvironment environment = MakeEnvironmentFromTheme(lava);

        EXPECT_FLOAT_EQ(environment.lightDirection.x, lava.lightDirection.x);
        EXPECT_FLOAT_EQ(environment.lightDirection.y, lava.lightDirection.y);
        EXPECT_FLOAT_EQ(environment.lightDirection.z, lava.lightDirection.z);
        EXPECT_FLOAT_EQ(environment.lightColor.x, lava.lightColor.x);
        EXPECT_FLOAT_EQ(environment.lightColor.y, lava.lightColor.y);
        EXPECT_FLOAT_EQ(environment.lightColor.z, lava.lightColor.z);
        EXPECT_FLOAT_EQ(environment.ambientColor.x, lava.ambientColor.x);
        EXPECT_FLOAT_EQ(environment.ambientColor.y, lava.ambientColor.y);
        EXPECT_FLOAT_EQ(environment.ambientColor.z, lava.ambientColor.z);
        EXPECT_EQ(environment.skyboxCubemapPath, lava.skyboxCubemapPath.generic_string());
        EXPECT_EQ(environment.blockTextureBaseSlice, lava.blockTextureArrayBaseSlice);
    }

    /// ファイル読込の退避系。TearDown で存在しないディレクトリを読ませ、全テーマを中立既定値へ戻す
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
        WriteTextFile(dir / "grass.asset", R"({
            "type": "theme",
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

    TEST_F(ThemeRegistryFileTest, MissingFileFallsToNeutralDefault)
    {
        // grass だけのディレクトリを読ませると、無い cave は中立の既定値 ThemeData{} になる
        const auto dir = MakeTempDir("missing");
        WriteTextFile(dir / "grass.asset", R"({"type": "theme", "displayName": "Meadow"})");

        LoadThemesFromDirectory(dir);

        ExpectThemeEq(Get(ThemeId::Cave), ThemeData{});
        EXPECT_EQ(Get(ThemeId::Grass).displayName, "Meadow");

        std::filesystem::remove_all(dir);
    }

    TEST_F(ThemeRegistryFileTest, BrokenFileFallsToNeutralDefault)
    {
        const auto dir = MakeTempDir("broken");
        WriteTextFile(dir / "grass.asset", "{ this is not json ,,,");

        LoadThemesFromDirectory(dir);

        ExpectThemeEq(Get(ThemeId::Grass), ThemeData{});

        std::filesystem::remove_all(dir);
    }

    TEST_F(ThemeRegistryFileTest, PartialFileKeepsNeutralDefaultsForMissingKeys)
    {
        const auto dir = MakeTempDir("partial");
        WriteTextFile(dir / "grass.asset", R"({"type": "theme", "lightColor": [0.1, 0.2, 0.3]})");

        LoadThemesFromDirectory(dir);

        const ThemeData neutral{};
        const ThemeData& grass = Get(ThemeId::Grass);
        EXPECT_FLOAT_EQ(grass.lightColor.x, 0.1f);
        EXPECT_FLOAT_EQ(grass.lightColor.y, 0.2f);
        EXPECT_FLOAT_EQ(grass.lightColor.z, 0.3f);
        // 書かれていないキーは中立の既定値のまま残る
        EXPECT_EQ(grass.displayName, neutral.displayName);
        EXPECT_EQ(grass.blockTextureArrayBaseSlice, neutral.blockTextureArrayBaseSlice);
        EXPECT_FLOAT_EQ(grass.lightDirection.x, neutral.lightDirection.x);
        EXPECT_FLOAT_EQ(grass.ambientColor.x, neutral.ambientColor.x);

        std::filesystem::remove_all(dir);
    }

    TEST_F(ThemeRegistryFileTest, UnknownKeysAreIgnored)
    {
        const auto dir = MakeTempDir("unknown");
        WriteTextFile(dir / "grass.asset", R"({
            "type": "theme",
            "displayName": "Meadow",
            "futureKey": {"nested": 1},
            "anotherUnknown": [1, 2, 3]
        })");

        LoadThemesFromDirectory(dir);

        EXPECT_EQ(Get(ThemeId::Grass).displayName, "Meadow");

        std::filesystem::remove_all(dir);
    }

    TEST_F(ThemeRegistryFileTest, ReloadRecoversAfterBrokenFileIsFixed)
    {
        // 壊れたファイルを直して再読込すれば、プロセスを跨がずファイル値へ戻れる
        const auto dir = MakeTempDir("recover");
        WriteTextFile(dir / "grass.asset", "{ broken ,,,");
        LoadThemesFromDirectory(dir);
        ExpectThemeEq(Get(ThemeId::Grass), ThemeData{});

        WriteTextFile(dir / "grass.asset", R"({"type": "theme", "displayName": "Meadow"})");
        LoadThemesFromDirectory(dir);
        EXPECT_EQ(Get(ThemeId::Grass).displayName, "Meadow");

        std::filesystem::remove_all(dir);
    }

    TEST_F(ThemeRegistryFileTest, NonThemeTypeIsIgnored)
    {
        // 種別欄が theme でない .asset は雛形として読まず、 その枠は中立の既定値のまま
        const auto dir = MakeTempDir("wrongtype");
        WriteTextFile(dir / "grass.asset", R"({"type": "material", "displayName": "Meadow"})");

        LoadThemesFromDirectory(dir);

        ExpectThemeEq(Get(ThemeId::Grass), ThemeData{});

        std::filesystem::remove_all(dir);
    }

    TEST_F(ThemeRegistryFileTest, MissingTypeIsIgnored)
    {
        // 種別欄が無い .asset も雛形として読まない。 種別欄は必須
        const auto dir = MakeTempDir("notype");
        WriteTextFile(dir / "grass.asset", R"({"displayName": "Meadow"})");

        LoadThemesFromDirectory(dir);

        ExpectThemeEq(Get(ThemeId::Grass), ThemeData{});

        std::filesystem::remove_all(dir);
    }
} // namespace
