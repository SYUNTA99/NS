#include <gtest/gtest.h>

#include <cstdint>

#include <Framework/Core/Filesystem.h>
#include <Framework/Core/Logger.h>
#include <Framework/Graphics/TextureArray.h>
#include <Game/Blocks/AutoTile.h>
#include <Game/Theme/ThemeId.h>
#include <Game/Theme/ThemeRegistry.h>

using namespace NS::Game::Theme;

namespace
{
    constexpr std::uint16_t kTotalSlices = NS::Graphics::TextureArray::kTotalSlices;

    /// slice 帯はテーマ値なので、同梱の `.theme` を読み込んだ状態で検証する
    class AutoTileThemeTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            NS::Core::Logger::Init();
            LoadThemesFromDirectory(NS::Core::FileSystem::ContentRoot() / "Assets" / "Themes");
        }
        void TearDown() override { NS::Core::Logger::Shutdown(); }
    };

    TEST_F(AutoTileThemeTest, AllBitmasksMapToValidSlice)
    {
        // 5 theme x 64 bitmask = 320 通り。 全て kTotalSlices (64) 未満の有効 slice index に丸まる事を確認
        for (std::uint16_t themeIdx = 0; themeIdx < static_cast<std::uint16_t>(ThemeId::Count); ++themeIdx)
        {
            const ThemeId theme = static_cast<ThemeId>(themeIdx);
            for (std::uint16_t mask = 0; mask < 64; ++mask)
            {
                const std::uint16_t slice =
                    NS::Game::Blocks::LookupTextureSlice(theme, static_cast<std::uint8_t>(mask));
                EXPECT_LT(slice, kTotalSlices)
                    << "theme=" << themeIdx << " mask=" << mask << " は kTotalSlices 未満に収まるべき";
            }
        }
    }

    TEST_F(AutoTileThemeTest, OutOfRangeThemeFallsBackToGrassSlice)
    {
        // ThemeId 99 のような範囲外指定でも crash せず、 Grass と同じ slice に丸まる
        const std::uint16_t grassSlice = NS::Game::Blocks::LookupTextureSlice(ThemeId::Grass, 0u);
        const std::uint16_t outOfRangeSlice = NS::Game::Blocks::LookupTextureSlice(static_cast<ThemeId>(99), 0u);
        EXPECT_EQ(outOfRangeSlice, grassSlice);
    }

    TEST_F(AutoTileThemeTest, OutOfRangeMaskFallsBackToSliceZero)
    {
        // bitmask は 6bit (上限 63) のはずだが、 ノイズが乗った 255 を渡しても落ちずに base slice にフォールバック
        const std::uint16_t slice =
            NS::Game::Blocks::LookupTextureSlice(ThemeId::Grass, static_cast<std::uint8_t>(255));
        EXPECT_EQ(slice, 0u);
    }

    TEST_F(AutoTileThemeTest, ThemesUseDifferentBaseSlices)
    {
        // mask=0 (孤立 block) は variant offset 0、 theme 別 base slice 値の違いがそのまま slice 番号差になる
        const std::uint16_t grassSlice = NS::Game::Blocks::LookupTextureSlice(ThemeId::Grass, 0u);
        const std::uint16_t caveSlice = NS::Game::Blocks::LookupTextureSlice(ThemeId::Cave, 0u);
        EXPECT_NE(grassSlice, caveSlice) << "Grass と Cave は別の base slice を持つはず";
    }
} // namespace
