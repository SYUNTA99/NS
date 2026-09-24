#include "Editor/PaletteTemplates.h"

#include "Editor/EditorObjects.h"
#include "Runtime/Object/Components/TransformComponent.h"

namespace NS::Editor
{
    const std::array<PaletteTemplate, k_PaletteSlotCount>& PaletteTemplateSlots() noexcept
    {
        static const std::array<PaletteTemplate, k_PaletteSlotCount> slots = []() {
            std::array<PaletteTemplate, k_PaletteSlotCount> result{};

            // スロット0: 基本の立方体ブロック
            {
                nlohmann::json cube = NS::Editor::MakeCellObject(0, 0, 0);
                result[0].name = "Cube";
                result[0].rotatable = NS::Editor::IsRotatableObject(cube);
                result[0].prototype = std::move(cube);
            }

            // スロット1: 45度傾斜のスロープ
            {
                nlohmann::json slope = NS::Editor::MakeCellObject(0, 0, 0);
                NS::Obj::ObjectJsonComponents(slope) = NS::Editor::MakeCellSlopeComponents(45.0f);
                NS::Obj::EnsureTransformComponent(slope);
                result[1].name = "Slope 45";
                result[1].rotatable = NS::Editor::IsRotatableObject(slope);
                result[1].prototype = std::move(slope);
            }

            // スロット2: レベルクリア判定を持つゴールオブジェクト
            {
                nlohmann::json goal = NS::Editor::MakeCellObject(0, 0, 0);
                NS::Obj::ObjectJsonComponents(goal) = NS::Editor::MakeGoalComponents();
                NS::Obj::EnsureTransformComponent(goal);
                result[2].name = "Goal";
                result[2].rotatable = NS::Editor::IsRotatableObject(goal);
                result[2].prototype = std::move(goal);
            }

            return result;
        }();

        return slots;
    }
} // namespace NS::Editor