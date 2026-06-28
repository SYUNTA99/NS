#pragma once

/// @file ThemeRegistry.h
/// @brief 5 テーマを静的に保有する read-only lookup (全テーマ視覚フィールドのみ)
///
/// @details `NS::Game::Theme::Get(id)` は 5 件の `ThemeData` への const 参照を返し、 範囲外は Grass
/// にフォールバックする 静的 storage 上に並ぶので呼出側は参照を frame 越しに保持して問題ない

#include <cstdint>

#include "Game/Theme/ThemeData.h"
#include "Game/Theme/ThemeId.h"

namespace NS::Game::Theme
{
    /// 5 テーマの中から id に対応する ThemeData を返す
    /// 範囲外 (>= Count) の場合は ThemeId::Grass にフォールバックする (入力境界での防御)
    [[nodiscard]] const ThemeData& Get(ThemeId id) noexcept;

    /// LevelData::themeId (uint16_t) からの取得。 範囲外は Grass にフォールバックする
    [[nodiscard]] const ThemeData& Get(std::uint16_t levelDataThemeId) noexcept;
} // namespace NS::Game::Theme
