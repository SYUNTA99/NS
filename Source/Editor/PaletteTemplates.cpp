#include "Editor/PaletteTemplates.h"

#include "Editor/PlacementCatalog.h"

#include <string_view>

namespace NS::Editor
{
    namespace
    {
        // 置ける物の一覧から 1 つを写してブラシにする。一覧に無い名前は空のブラシのまま
        void FillSlot(PaletteTemplate& slot, const char* name, std::string_view label)
        {
            slot.name = name;
            if (const PlacementItem* item = FindPlacementItem(label))
            {
                slot.rotatable = item->rotatable;
                slot.prototype = item->prototype;
            }
        }
    } // namespace

    const std::array<PaletteTemplate, k_PaletteSlotCount>& PaletteTemplateSlots() noexcept
    {
        static const std::array<PaletteTemplate, k_PaletteSlotCount> slots = []() {
            std::array<PaletteTemplate, k_PaletteSlotCount> result{};
            // 地形の部品の立方体・45 度の坂と、ゴール
            FillSlot(result[0], "立方体", k_PartsCubeLabel);
            FillSlot(result[1], "坂 45", k_PartsSlopeLabel);
            FillSlot(result[2], "ゴール", "ゴール");
            return result;
        }();
        return slots;
    }
} // namespace NS::Editor
