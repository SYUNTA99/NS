#include <gtest/gtest.h>

#include <cstdint>

#include <Framework/Graphics/TextureArray.h>
#include <GameCore/Blocks/AutoTile.h>

namespace
{
    constexpr std::uint16_t kTotalSlices = NS::Graphics::TextureArray::kTotalSlices;

    // 同梱テーマが使う 5 本の slice 帯先頭。 焼き込みはこの帯からのオフセットで slice を引く
    constexpr std::uint16_t kThemeBaseSlices[5] = {0, 8, 16, 24, 32};

    TEST(AutoTileSliceTest, AllBitmasksMapToValidSlice)
    {
        // 5 帯 x 64 bitmask = 320 通り。 全て kTotalSlices 未満の有効 slice index に丸まる事を確認
        for (const std::uint16_t baseSlice : kThemeBaseSlices)
        {
            for (std::uint16_t mask = 0; mask < 64; ++mask)
            {
                const std::uint16_t slice =
                    NS::GameCore::Blocks::LookupTextureSlice(baseSlice, static_cast<std::uint8_t>(mask));
                EXPECT_LT(slice, kTotalSlices)
                    << "baseSlice=" << baseSlice << " mask=" << mask << " は kTotalSlices 未満に収まるべき";
            }
        }
    }

    TEST(AutoTileSliceTest, OutOfRangeBaseSliceFallsBackToSliceZero)
    {
        // 総 slice 数以上の帯先頭を渡しても crash せず 0 に倒れる。 variant 加算側も mask>=64 の直接返し側も
        const std::uint16_t withVariant = NS::GameCore::Blocks::LookupTextureSlice(kTotalSlices, 0u);
        const std::uint16_t withoutVariant = NS::GameCore::Blocks::LookupTextureSlice(kTotalSlices, 255u);
        EXPECT_EQ(withVariant, 0u);
        EXPECT_EQ(withoutVariant, 0u);
    }

    TEST(AutoTileSliceTest, OutOfRangeMaskFallsBackToBaseSlice)
    {
        // bitmask は 6bit (上限 63) のはずだが、 ノイズが乗った 255 を渡しても落ちず variant 加算なしの帯先頭を返す
        const std::uint16_t slice = NS::GameCore::Blocks::LookupTextureSlice(8u, static_cast<std::uint8_t>(255));
        EXPECT_EQ(slice, 8u);
    }

    TEST(AutoTileSliceTest, DifferentBaseSlicesProduceDifferentSlice)
    {
        // mask=0 (孤立 block) は variant offset 0 なので、 帯先頭の違いがそのまま slice 番号差になる
        const std::uint16_t grassSlice = NS::GameCore::Blocks::LookupTextureSlice(0u, 0u);
        const std::uint16_t caveSlice = NS::GameCore::Blocks::LookupTextureSlice(8u, 0u);
        EXPECT_NE(grassSlice, caveSlice) << "別の帯先頭は別の slice を指すはず";
    }
} // namespace
