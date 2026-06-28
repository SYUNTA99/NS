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
    /// name は toolbar 表示名、 rotatable は R で回せるか、 prototype は複製元
    struct PaletteTemplate
    {
        const char* name = nullptr;
        bool rotatable = false;
        NS::Game::Level::ObjectInstance prototype;
    };

    /// パレット 1 スロット。 grid に置く素の cube。 プレイヤー配置はギズモで実プレイヤーを動かすため別経路
    [[nodiscard]] const std::array<PaletteTemplate, 1>& PaletteTemplateSlots() noexcept;
} // namespace NS::Editor
