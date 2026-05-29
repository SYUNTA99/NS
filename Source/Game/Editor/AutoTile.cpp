#include "Game/Editor/AutoTile.h"

#include "Framework/Graphics/TextureArray.h"
#include "Game/Level/LevelData.h"
#include "Game/Theme/ThemeRegistry.h"

#include <cstddef>

namespace NS::Game::Editor
{

    namespace
    {
        // 6-neighbor bitmask (64 通り) を 8 variant (0..7) に縮約するテーブル。
        // popcount で「埋まり具合」 を見て大まかな variant に丸める素朴な対応付け。
        // - 0 neighbor (孤立 cube): variant 0 = isolated
        // - 1-2 neighbor (端 / 角): variant 1-2
        // - 3-4 neighbor (壁 / 床中央): variant 3-4
        // - 5 neighbor (T 字): variant 5-6
        // - 6 neighbor (完全埋没): variant 7 = interior
        // popcount が同じでも上下軸 (bit 2 / 3) の有無で variant をずらし、 上面が
        // 見えるケースだけは「天井あり」 風の variant に飛ばす。 詳細チューニングは
        // designer 介入で差し替え可能、 ここでは全 64 mask が valid variant に丸まる事を保証する。
        constexpr std::uint8_t kBitmaskToVariant[64] = {
            // bit 0..3..5 popcount (mask): variant
            // mask 0 = 000000: 0 (isolated)
            // mask が +X (bit0) のみなどの 1-neighbor: 1
            // 2-neighbor: 2
            // 3-neighbor: 3 / 4 (上向き面の有無で分ける)
            // 4-neighbor: 4 / 5
            // 5-neighbor: 5 / 6
            // 6-neighbor: 7 (interior)
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
        // theme 範囲外 → Grass ( 互換の入力境界 fallback)。
        if (static_cast<std::size_t>(theme) >= static_cast<std::size_t>(ThemeId::Count))
        {
            theme = ThemeId::Grass;
        }
        // bitmask 範囲外 (>=64) は base slice (offset 0) にフォールバック。
        if (neighborMask >= 64)
        {
            const ThemeData& td0 = ThemeRegistry::Get(theme);
            const std::uint16_t base0 = td0.blockTextureArrayBaseSlice;
            return base0 < NS::Graphics::TextureArray::kTotalSlices ? base0 : static_cast<std::uint16_t>(0);
        }

        const ThemeData& td = ThemeRegistry::Get(theme);
        const std::uint16_t base = td.blockTextureArrayBaseSlice;
        const std::uint8_t variant = kBitmaskToVariant[neighborMask];
        // variant が 8 を超えないテーブルを書いてあるが、 念のため clamp する。
        const std::uint8_t safeVariant = variant < kVariantsPerTheme ? variant : static_cast<std::uint8_t>(0);
        const std::uint32_t slice = static_cast<std::uint32_t>(base) + static_cast<std::uint32_t>(safeVariant);
        if (slice >= NS::Graphics::TextureArray::kTotalSlices)
        {
            return 0; // 念のため範囲外 clamp。
        }
        return static_cast<std::uint16_t>(slice);
    }

    std::uint8_t ComputeNeighborMask(const NS::Game::Level::LevelData& level,
                                     std::int16_t x,
                                     std::int16_t y,
                                     std::int16_t z,
                                     std::uint16_t blockId) noexcept
    {
        // bit 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z の順で 6 方向。
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
