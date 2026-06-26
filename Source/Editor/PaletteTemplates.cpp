#include "Editor/PaletteTemplates.h"

#include "Game/Blocks/BlockRegistry.h"

namespace NS::Editor
{
    NS::Game::Level::ObjectInstance PaletteTemplateForKind(std::uint16_t kind) noexcept
    {
        if (kind == NS::Game::Blocks::kBlockIdSpawn)
        {
            // spawn は LevelData::spawnX/Y/Z を上書きする marker で、 grid object として積まない
            NS::Game::Level::ObjectInstance marker{};
            marker.kind = NS::Game::Blocks::kBlockIdSpawn;
            marker.materialIndex = -1;
            marker.flags = 0;
            return marker;
        }
        return NS::Game::Level::MakeGridObject(0, 0, 0, kind, 0);
    }

    const char* SlopeVariantName(std::uint16_t slopeKind) noexcept
    {
        switch (slopeKind)
        {
        case NS::Game::Blocks::kBlockIdSlope45:
            return "Slope 45";
        case NS::Game::Blocks::kBlockIdSlope30:
            return "Slope 30";
        case NS::Game::Blocks::kBlockIdSlope22:
            return "Slope 22.5";
        case NS::Game::Blocks::kBlockIdSlope15:
            return "Slope 15";
        default:
            return "Slope";
        }
    }

    const std::array<PaletteTemplate, 8>& PaletteTemplateSlots() noexcept
    {
        namespace Blk = NS::Game::Blocks;
        static const std::array<PaletteTemplate, 8> slots = {{
            {"Solid", PaletteTemplateForKind(Blk::kBlockIdSolid)},
            {"Coin", PaletteTemplateForKind(Blk::kBlockIdCoin)},
            {"Star", PaletteTemplateForKind(Blk::kBlockIdPowerStar)},
            {"Spawn", PaletteTemplateForKind(Blk::kBlockIdSpawn)},
            {"Slope 45", PaletteTemplateForKind(Blk::kBlockIdSlope45)},
            {"Pole", PaletteTemplateForKind(Blk::kBlockIdPole)},
            {"Hazard", PaletteTemplateForKind(Blk::kBlockIdHazard)},
            {"Water", PaletteTemplateForKind(Blk::kBlockIdWater)},
        }};
        return slots;
    }
} // namespace NS::Editor
