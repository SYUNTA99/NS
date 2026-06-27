#include "Editor/PaletteTemplates.h"

#include "Game/Blocks/BlockRegistry.h"
#include "Game/Blocks/BuildPlacedObject.h"

#include <utility>

namespace NS::Editor
{
    namespace
    {
        // パレットが自前で持つ表示名。 配置物の種別名を kind から引かず palette 内で確定する
        const char* NameForKind(std::uint16_t kind) noexcept
        {
            namespace Blk = NS::Game::Blocks;
            if (Blk::IsSlopeBlock(kind))
                return SlopeVariantName(kind);
            switch (kind)
            {
            case Blk::kBlockIdSolid:
                return "Solid";
            case Blk::kBlockIdCoin:
                return "Coin";
            case Blk::kBlockIdPowerStar:
                return "Star";
            case Blk::kBlockIdSpawn:
                return "Spawn";
            case Blk::kBlockIdPole:
                return "Pole";
            case Blk::kBlockIdHazard:
                return "Hazard";
            case Blk::kBlockIdWater:
                return "Water";
            case Blk::kBlockIdDecoration:
                return "Decoration";
            default:
                return "?";
            }
        }
    } // namespace

    PaletteTemplate PaletteTemplateForKind(std::uint16_t kind) noexcept
    {
        PaletteTemplate tmpl{};
        tmpl.name = NameForKind(kind);
        tmpl.isSpawn = (kind == NS::Game::Blocks::kBlockIdSpawn);
        tmpl.rotatable = NS::Game::Blocks::IsRotatableBlock(kind);

        if (tmpl.isSpawn)
        {
            // spawn は LevelData::spawnX/Y/Z を上書きする marker で、 grid object として積まない
            NS::Game::Level::ObjectInstance marker{};
            marker.materialIndex = -1;
            marker.flags = 0;
            tmpl.prototype = std::move(marker);
            return tmpl;
        }

        // prototype は cell 原点の grid 配置物。 kind が決める mesh / 当たり / 拾得を実 component へ展開して持つ
        NS::Game::Level::ObjectInstance prototype = NS::Game::Level::MakeGridObject(0, 0, 0, kind, 0);
        prototype.components = NS::Game::Blocks::MaterializeLegacyKind(kind, prototype);
        tmpl.prototype = std::move(prototype);
        return tmpl;
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
            PaletteTemplateForKind(Blk::kBlockIdSolid),
            PaletteTemplateForKind(Blk::kBlockIdCoin),
            PaletteTemplateForKind(Blk::kBlockIdPowerStar),
            PaletteTemplateForKind(Blk::kBlockIdSpawn),
            PaletteTemplateForKind(Blk::kBlockIdSlope45),
            PaletteTemplateForKind(Blk::kBlockIdPole),
            PaletteTemplateForKind(Blk::kBlockIdHazard),
            PaletteTemplateForKind(Blk::kBlockIdWater),
        }};
        return slots;
    }
} // namespace NS::Editor
