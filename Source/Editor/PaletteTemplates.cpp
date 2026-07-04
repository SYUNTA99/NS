#include "Editor/PaletteTemplates.h"

#include "Game/Blocks/BuildPlacedObject.h"

#include <array>
#include <utility>

namespace NS::Editor
{
    const std::array<PaletteTemplate, kPaletteSlotCount>& PaletteTemplateSlots() noexcept
    {
        static const std::array<PaletteTemplate, kPaletteSlotCount> slots = []() {
            std::array<PaletteTemplate, kPaletteSlotCount> result{};

            // slot0: grid に置く素の cube。 回転可否は組み上がった component から導く
            NS::Game::Level::ObjectInstance cube = NS::Game::Level::MakeGridObject(0, 0, 0, 0);
            result[0].name = "Cube";
            result[0].rotatable = NS::Game::Blocks::IsRotatableObject(cube);
            result[0].prototype = std::move(cube);

            // slot1: 45 度スロープ。 grid セルへ楔メッシュ + SlopeCollider を載せ、 向きは R で回せる
            NS::Game::Level::ObjectInstance slope = NS::Game::Level::MakeGridObject(0, 0, 0, 0);
            slope.components = NS::Game::Blocks::MakeGridSlopeComponents(45.0f);
            result[1].name = "Slope 45";
            result[1].rotatable = NS::Game::Blocks::IsRotatableObject(slope);
            result[1].prototype = std::move(slope);

            // slot2: ゴール。 接触でレベルクリアになる pickup。 向きは無関係なので回転不可
            NS::Game::Level::ObjectInstance goal = NS::Game::Level::MakeGridObject(0, 0, 0, 0);
            goal.components = NS::Game::Blocks::MakeGoalComponents();
            result[2].name = "Goal";
            result[2].rotatable = NS::Game::Blocks::IsRotatableObject(goal);
            result[2].prototype = std::move(goal);

            return result;
        }();
        return slots;
    }
} // namespace NS::Editor
