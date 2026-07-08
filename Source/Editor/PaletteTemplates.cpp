#include "Editor/PaletteTemplates.h"

#include "GameCore/Blocks/BuildPlacedObject.h"

namespace NS::Editor
{
    const std::array<PaletteTemplate, kPaletteSlotCount>& PaletteTemplateSlots() noexcept
    {
        static const std::array<PaletteTemplate, kPaletteSlotCount> slots = []() {
            std::array<PaletteTemplate, kPaletteSlotCount> result{};

            // slot0: grid に置く素の cube
            NS::GameCore::Level::ObjectInstance cube = NS::GameCore::Level::MakeCellObject(0, 0, 0, 0);
            result[0].name = "Cube";
            result[0].rotatable = NS::GameCore::Blocks::IsRotatableObject(cube);
            result[0].prototype = std::move(cube);

            // slot1: 45 度スロープ、 向きは R で回せる
            NS::GameCore::Level::ObjectInstance slope = NS::GameCore::Level::MakeCellObject(0, 0, 0, 0);
            slope.components = NS::GameCore::Blocks::MakeCellSlopeComponents(45.0f);
            result[1].name = "Slope 45";
            result[1].rotatable = NS::GameCore::Blocks::IsRotatableObject(slope);
            result[1].prototype = std::move(slope);

            // slot2: ゴール、 接触でレベルクリアになる pickup
            NS::GameCore::Level::ObjectInstance goal = NS::GameCore::Level::MakeCellObject(0, 0, 0, 0);
            goal.components = NS::GameCore::Blocks::MakeGoalComponents();
            result[2].name = "Goal";
            result[2].rotatable = NS::GameCore::Blocks::IsRotatableObject(goal);
            result[2].prototype = std::move(goal);

            return result;
        }();
        return slots;
    }
} // namespace NS::Editor
