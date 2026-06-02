#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

#include <Game/Level/LevelData.h>
#include <Game/Theme/ThemeData.h>
#include <Game/Theme/ThemeId.h>
#include <Game/Theme/ThemeRegistry.h>

namespace
{
    TEST(ThemeRegistryTest, FiveThemesExist)
    {
        EXPECT_EQ(static_cast<int>(ThemeId::Count), 5);
    }

    TEST(ThemeRegistryTest, AllThemesDistinct)
    {
        const ThemeData& grass = ThemeRegistry::Get(ThemeId::Grass);
        const ThemeData& cave = ThemeRegistry::Get(ThemeId::Cave);
        const ThemeData& snow = ThemeRegistry::Get(ThemeId::Snow);
        const ThemeData& lava = ThemeRegistry::Get(ThemeId::Lava);
        const ThemeData& sky = ThemeRegistry::Get(ThemeId::Sky);

        // 表示名は全て異なる文字列を指していること
        ASSERT_NE(grass.displayName, nullptr);
        ASSERT_NE(cave.displayName, nullptr);
        ASSERT_NE(snow.displayName, nullptr);
        ASSERT_NE(lava.displayName, nullptr);
        ASSERT_NE(sky.displayName, nullptr);

        EXPECT_STRNE(grass.displayName, cave.displayName);
        EXPECT_STRNE(grass.displayName, snow.displayName);
        EXPECT_STRNE(grass.displayName, lava.displayName);
        EXPECT_STRNE(grass.displayName, sky.displayName);
        EXPECT_STRNE(cave.displayName, snow.displayName);
        EXPECT_STRNE(cave.displayName, lava.displayName);
        EXPECT_STRNE(cave.displayName, sky.displayName);
        EXPECT_STRNE(snow.displayName, lava.displayName);
        EXPECT_STRNE(snow.displayName, sky.displayName);
        EXPECT_STRNE(lava.displayName, sky.displayName);

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
        const ThemeData& grass = ThemeRegistry::Get(ThemeId::Grass);

        const ThemeData& byEnum = ThemeRegistry::Get(static_cast<ThemeId>(99));
        const ThemeData& byUint16 = ThemeRegistry::Get(static_cast<std::uint16_t>(999));

        EXPECT_EQ(&byEnum, &grass);
        EXPECT_EQ(&byUint16, &grass);
        EXPECT_STREQ(byEnum.displayName, grass.displayName);
        EXPECT_STREQ(byUint16.displayName, grass.displayName);
    }

    TEST(ThemeRegistryTest, LevelDataThemeIdRoundTrip)
    {
        NS::Game::Level::LevelData level{};
        level.themeId = 3;
        const ThemeData& theme = ThemeRegistry::Get(level.themeId);
        EXPECT_STREQ(theme.displayName, "Lava");
    }
} // namespace
