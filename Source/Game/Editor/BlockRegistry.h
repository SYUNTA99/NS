#pragma once

/// @file BlockRegistry.h
/// @brief 編集時 vertical slice の 1 block type + 3 marker type の ID 定義。
///
/// @details `LevelData::BlockEntry::blockId` に格納される値。 ID 体系を
/// テクスチャテーブル / 振る舞いテーブル等に流用する想定で、 今後の
///  で本格的な block type 群を追加するまでの placeholder。

#include "Framework/Core/Math.h"

#include <cstdint>

namespace NS::Game::Editor
{
    /// 通常の固形ブロック (placeholder、 cube_test テクスチャを使い回す)。
    inline constexpr std::uint16_t kBlockIdSolid = 1;

    /// 取得アイテムのコイン。
    inline constexpr std::uint16_t kBlockIdCoin = 100;

    /// クリア条件にもなり得るパワースター。
    inline constexpr std::uint16_t kBlockIdPowerStar = 101;

    /// toolbar の表示用識別子。 実体は `LevelData::spawnX/Y/Z` に書く。
    inline constexpr std::uint16_t kBlockIdSpawn = 102;

    /// 各 ID に紐づく Toolbar 表示名 (ASCII 固定で ImGui label 直渡し可能)。
    [[nodiscard]] const char* GetDisplayName(std::uint16_t blockId) noexcept;

    /// 各 ID に紐づく base color (RGBA float)。 テクスチャが揃うまでの色分け用。
    [[nodiscard]] NS::Core::Color GetBaseColor(std::uint16_t blockId) noexcept;
} // namespace NS::Game::Editor
