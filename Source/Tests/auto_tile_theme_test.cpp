#include <gtest/gtest.h>

#include <cstdint>

#include <Framework/Graphics/TextureArray.h>
#include <Game/Editor/AutoTile.h>
#include <Game/Editor/BlockRegistry.h>
#include <Game/Theme/ThemeId.h>

namespace
{
    constexpr std::uint16_t kTotalSlices = NS::Graphics::TextureArray::kTotalSlices;

    TEST(AutoTileThemeTest, AllBitmasksMapToValidSlice)
    {
        // 5 theme x 64 bitmask = 320 通り。 全て kTotalSlices (64) 未満の有効 slice index に丸まる事を確認。
        for (std::uint16_t themeIdx = 0; themeIdx < static_cast<std::uint16_t>(ThemeId::Count); ++themeIdx)
        {
            const ThemeId theme = static_cast<ThemeId>(themeIdx);
            for (std::uint16_t mask = 0; mask < 64; ++mask)
            {
                const std::uint16_t slice = NS::Game::Editor::LookupTextureSlice(
                    theme, static_cast<std::uint8_t>(mask), NS::Game::Editor::kBlockIdSolid);
                EXPECT_LT(slice, kTotalSlices)
                    << "theme=" << themeIdx << " mask=" << mask << " は kTotalSlices 未満に収まるべき";
            }
        }
    }

    TEST(AutoTileThemeTest, OutOfRangeThemeFallsBackToGrassSlice)
    {
        // ThemeId 99 のような範囲外指定でも crash せず、 Grass と同じ slice に丸まる。
        const std::uint16_t grassSlice =
            NS::Game::Editor::LookupTextureSlice(ThemeId::Grass, 0u, NS::Game::Editor::kBlockIdSolid);
        const std::uint16_t outOfRangeSlice =
            NS::Game::Editor::LookupTextureSlice(static_cast<ThemeId>(99), 0u, NS::Game::Editor::kBlockIdSolid);
        EXPECT_EQ(outOfRangeSlice, grassSlice);
    }

    TEST(AutoTileThemeTest, OutOfRangeMaskFallsBackToSliceZero)
    {
        // bitmask は 6bit (上限 63) のはずだが、 ノイズが乗った 255 を渡しても落ちずに base slice にフォールバック。
        const std::uint16_t slice = NS::Game::Editor::LookupTextureSlice(
            ThemeId::Grass, static_cast<std::uint8_t>(255), NS::Game::Editor::kBlockIdSolid);
        EXPECT_EQ(slice, 0u);
    }

    TEST(AutoTileThemeTest, ThemesUseDifferentBaseSlices)
    {
        // mask=0 (孤立 block) は variant offset 0、 theme 別 base slice 値の違いがそのまま slice 番号差になる。
        const std::uint16_t grassSlice =
            NS::Game::Editor::LookupTextureSlice(ThemeId::Grass, 0u, NS::Game::Editor::kBlockIdSolid);
        const std::uint16_t caveSlice =
            NS::Game::Editor::LookupTextureSlice(ThemeId::Cave, 0u, NS::Game::Editor::kBlockIdSolid);
        EXPECT_NE(grassSlice, caveSlice) << "Grass と Cave は別の base slice を持つはず";
    }
} // namespace
