#include "Game/Editor/AutoTile.h"

#include "Game/Level/LevelData.h"

namespace NS::Game::Editor
{

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
