#pragma once

/// @file ComponentClipboard.h
/// @brief runtime Component の現在値を保存形式 ComponentData へ写し取る橋渡し

#include "Game/Level/LevelData.h"

namespace NS::Scene
{
    class Component;
} // namespace NS::Scene

namespace NS::Editor
{
    /// comp の反射フィールドを今の値ごと ComponentData へ写す
    /// @details Inspector でライブ編集した値もそのまま拾う。 反射を持たない Component は型名も fields も空のまま返る
    [[nodiscard]] NS::Game::Level::ComponentData CaptureComponentData(const NS::Scene::Component& comp);
} // namespace NS::Editor
