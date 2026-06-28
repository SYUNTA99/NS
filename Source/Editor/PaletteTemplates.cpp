#include "Editor/PaletteTemplates.h"

#include "Game/Blocks/BuildPlacedObject.h"

#include <array>
#include <utility>

namespace NS::Editor
{
    const std::array<PaletteTemplate, 2>& PaletteTemplateSlots() noexcept
    {
        static const std::array<PaletteTemplate, 2> slots = []() {
            std::array<PaletteTemplate, 2> result{};

            // slot0: grid に置く素の cube。 回転可否は組み上がった component から導く
            NS::Game::Level::ObjectInstance cube = NS::Game::Level::MakeGridObject(0, 0, 0, 0);
            result[0].name = "Cube";
            result[0].isSpawn = false;
            result[0].rotatable = NS::Game::Blocks::IsRotatableObject(cube);
            result[0].prototype = std::move(cube);

            // slot1: spawn は LevelData::spawnX/Y/Z を上書きする marker で grid object として積まない
            NS::Game::Level::ObjectInstance marker{};
            marker.materialIndex = -1;
            marker.flags = 0;
            result[1].name = "Spawn";
            result[1].isSpawn = true;
            result[1].rotatable = false;
            result[1].prototype = std::move(marker);

            return result;
        }();
        return slots;
    }
} // namespace NS::Editor
