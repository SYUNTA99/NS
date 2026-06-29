#pragma once

/// @file PaletteTemplates.h
/// @brief パレットの配置プロトタイプ ObjectInstance テンプレート
///
/// @details パレットで何を置くかを実 component を持つプロトタイプの配置物として持つ
/// 各スロットは複製元の ObjectInstance を 1 つ抱え、 配置時はこれを複製して座標 / 回転を焼く

#include "Game/Level/LevelData.h"

#include <array>
#include <cstddef>

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

    /// パレットのブラシ数。 grid cube / 45 度スロープ / ゴールの 3 種
    inline constexpr std::size_t kPaletteSlotCount = 3;

    /// パレットの配置ブラシ一覧。 プレイヤー配置はギズモで実プレイヤーを動かすため別経路
    [[nodiscard]] const std::array<PaletteTemplate, kPaletteSlotCount>& PaletteTemplateSlots() noexcept;
} // namespace NS::Editor
