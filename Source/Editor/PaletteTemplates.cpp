#include "Editor/PaletteTemplates.h"

#include "Editor/EditorObjects.h"
#include "Game/Level/BlockObject.h"
#include "Runtime/Object/Components/TransformComponent.h"

namespace NS::Editor
{
    const std::array<PaletteTemplate, k_PaletteSlotCount>& PaletteTemplateSlots() noexcept
    {
        // テンプレート定義は実行時に一度だけ初期化する
        static const std::array<PaletteTemplate, k_PaletteSlotCount> slots = []() {
            std::array<PaletteTemplate, k_PaletteSlotCount> result{};

            // スロット0: 基本の正方形ブロック
            {
                NS::Object::ObjectData cube = NS::Game::Level::MakeCellObject(0, 0, 0);
                result[0].name = "Cube";
                result[0].rotatable = NS::Editor::IsRotatableObject(cube);
                result[0].prototype = std::move(cube);
            }

            // スロット1: 45度傾斜のスロープ
            {
                NS::Object::ObjectData slope = NS::Game::Level::MakeCellObject(0, 0, 0);
                slope.components = NS::Editor::MakeCellSlopeComponents(45.0f);
                NS::Object::EnsureTransformComponent(slope);
                result[1].name = "Slope 45";
                result[1].rotatable = NS::Editor::IsRotatableObject(slope);
                result[1].prototype = std::move(slope);
            }

            // スロット2: レベルクリア判定を持つゴールオブジェクト
            {
                NS::Object::ObjectData goal = NS::Game::Level::MakeCellObject(0, 0, 0);
                goal.components = NS::Editor::MakeGoalComponents();
                NS::Object::EnsureTransformComponent(goal);
                result[2].name = "Goal";
                result[2].rotatable = NS::Editor::IsRotatableObject(goal);
                result[2].prototype = std::move(goal);
            }

            return result;
        }();

        return slots;
    }
} // namespace NS::Editor