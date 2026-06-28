#include "Editor/PaletteTemplates.h"

#include "Game/Blocks/BuildPlacedObject.h"

#include <array>
#include <utility>

namespace NS::Editor
{
    const std::array<PaletteTemplate, 1>& PaletteTemplateSlots() noexcept
    {
        static const std::array<PaletteTemplate, 1> slots = []() {
            std::array<PaletteTemplate, 1> result{};

            // slot0: grid に置く素の cube。 回転可否は組み上がった component から導く
            NS::Game::Level::ObjectInstance cube = NS::Game::Level::MakeGridObject(0, 0, 0, 0);
            result[0].name = "Cube";
            result[0].rotatable = NS::Game::Blocks::IsRotatableObject(cube);
            result[0].prototype = std::move(cube);

            return result;
        }();
        return slots;
    }
} // namespace NS::Editor
