#pragma once

/// @file PaletteTemplates.h
/// @brief パレット 8 スロットのプロトタイプ ObjectInstance テンプレート
///
/// @details 「パレットで何を置くか」を kind 値ではなくプロトタイプの配置物として持つ
/// 各スロットは複製元の `ObjectInstance` を 1 つ抱え、 配置時はこれを複製して座標 / 回転を焼く
/// prototype は kind ではなく実 component 一覧を持ち、 表示名 / spawn / 回転可否はこのレジストリが持つ

#include "Game/Level/LevelData.h"

#include <array>
#include <cstddef>
#include <cstdint>

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

    /// パレット固定 8 スロット (固形 / コイン / スター / spawn / slope / pole / hazard / water)
    /// slope スロットは 45° を既定に持ち、 variant 循環で 30 / 22.5 / 15° へ差し替わる
    [[nodiscard]] const std::array<PaletteTemplate, 8>& PaletteTemplateSlots() noexcept;

    /// kind に対応する配置テンプレートを作る。 prototype は実 component 入り (spawn は世界に 1 点の marker)
    [[nodiscard]] PaletteTemplate PaletteTemplateForKind(std::uint16_t kind) noexcept;

    /// slope kind の表示名 (45 / 30 / 22.5 / 15)。 slope でなければ "Slope"
    [[nodiscard]] const char* SlopeVariantName(std::uint16_t slopeKind) noexcept;
} // namespace NS::Editor
