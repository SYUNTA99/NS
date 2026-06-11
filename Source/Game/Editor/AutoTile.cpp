#include "Game/Editor/AutoTile.h"

#include "Framework/Graphics/TextureArray.h"
#include "Game/Level/LevelData.h"
#include "Game/Theme/ThemeRegistry.h"

#include <cstddef>

namespace NS::Game::Editor
{

    namespace
    {
        // 6-neighbor bitmask(64 通り)を popcount ベースで 8 variant(0=孤立〜7=完全埋没)に縮約するテーブル
        // 上下軸 bit(2/3)の有無で同 popcount でも variant をずらして天井面を区別する
        constexpr std::uint8_t kBitmaskToVariant[64] = {
            0, 1, 1, 2, 1, 2, 2, 3, // 0..7
            3, 4, 4, 5, 4, 5, 5, 6, // 8..15  (bit 3 = -Y、 床に埋まる)
            1, 2, 2, 3, 2, 3, 3, 4, // 16..23 (bit 4 = +Z)
            4, 5, 5, 6, 5, 6, 6, 7, // 24..31
            1, 2, 2, 3, 2, 3, 3, 4, // 32..39 (bit 5 = -Z)
            4, 5, 5, 6, 5, 6, 6, 7, // 40..47
            2, 3, 3, 4, 3, 4, 4, 5, // 48..55 (+Z + -Z = 通路)
            5, 6, 6, 7, 6, 7, 7, 7, // 56..63 (完全埋没側)
        };
        constexpr std::uint8_t kVariantsPerTheme = 8;
    } // namespace

    std::uint16_t LookupTextureSlice(ThemeId theme, std::uint8_t neighborMask, std::uint16_t /*blockId*/) noexcept
    {
        // theme 範囲外 → Grass (入力境界の fallback)
        if (static_cast<std::size_t>(theme) >= static_cast<std::size_t>(ThemeId::Count))
        {
            theme = ThemeId::Grass;
        }
        // bitmask 範囲外 (>=64) は base slice (offset 0) にフォールバック
        if (neighborMask >= 64)
        {
            const ThemeData& td0 = ThemeRegistry::Get(theme);
            const std::uint16_t base0 = td0.blockTextureArrayBaseSlice;
            return base0 < NS::Graphics::TextureArray::kTotalSlices ? base0 : static_cast<std::uint16_t>(0);
        }

        const ThemeData& td = ThemeRegistry::Get(theme);
        const std::uint16_t base = td.blockTextureArrayBaseSlice;
        const std::uint8_t variant = kBitmaskToVariant[neighborMask];
        // variant が 8 を超えないテーブルを書いてあるが、 念のため clamp する
        const std::uint8_t safeVariant = variant < kVariantsPerTheme ? variant : static_cast<std::uint8_t>(0);
        const std::uint32_t slice = static_cast<std::uint32_t>(base) + static_cast<std::uint32_t>(safeVariant);
        if (slice >= NS::Graphics::TextureArray::kTotalSlices)
        {
            return 0; // 念のため範囲外 clamp
        }
        return static_cast<std::uint16_t>(slice);
    }

    std::uint8_t ComputeNeighborMask(const NS::Game::Level::LevelData& level,
                                     std::int16_t x,
                                     std::int16_t y,
                                     std::int16_t z,
                                     std::uint16_t blockId) noexcept
    {
        // bit 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z の順で 6 方向
        static constexpr std::int16_t kOffsets[6][3] = {
            {+1, 0, 0},
            {-1, 0, 0},
            {0, +1, 0},
            {0, -1, 0},
            {0, 0, +1},
            {0, 0, -1},
        };

        std::uint8_t mask = 0;
        for (int i = 0; i < 6; ++i)
        {
            const std::int16_t nx = static_cast<std::int16_t>(x + kOffsets[i][0]);
            const std::int16_t ny = static_cast<std::int16_t>(y + kOffsets[i][1]);
            const std::int16_t nz = static_cast<std::int16_t>(z + kOffsets[i][2]);
            for (const auto& b : level.blocks)
            {
                if (b.x == nx && b.y == ny && b.z == nz && b.blockId == blockId)
                {
                    mask = static_cast<std::uint8_t>(mask | (1u << i));
                    break;
                }
            }
        }
        return mask;
    }

    void SetSpawnMarker(NS::Game::Level::LevelData& level, std::int16_t x, std::int16_t y, std::int16_t z) noexcept
    {
        level.spawnX = x;
        level.spawnY = y;
        level.spawnZ = z;
    }

} // namespace NS::Game::Editor
