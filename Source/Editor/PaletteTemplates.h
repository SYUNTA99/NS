#pragma once

/// @file PaletteTemplates.h
/// @brief パレットの配置プロトタイプ ObjectInstance テンプレート
///
/// @details パレットで何を置くかを実 component を持つプロトタイプの配置物として持つ
/// 各スロットは複製元の ObjectInstance を 1 つ抱え、 配置時はこれを複製して座標 / 回転を焼く

#include "Game/Level/LevelData.h"

#include <array>

namespace NS::Editor
{
    /// パレット 1 スロットの配置プロトタイプ
    /// name は toolbar 表示名、 isSpawn は spawn marker か、 rotatable は R で回せるか、 prototype は複製元
    struct PaletteTemplate
    {
        const char* name = nullptr;
        bool isSpawn = false;
        bool rotatable = false;
        NS::Game::Level::ObjectInstance prototype;
    };

    /// パレット 2 スロット。 slot0 は grid に置く素の cube、 slot1 は世界に 1 点の spawn marker
    [[nodiscard]] const std::array<PaletteTemplate, 2>& PaletteTemplateSlots() noexcept;
} // namespace NS::Editor
