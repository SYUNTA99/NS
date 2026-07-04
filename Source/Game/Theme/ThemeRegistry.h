#pragma once

/// @file ThemeRegistry.h
/// @brief 視覚フィールドを保有する 5 テーマの lookup。 `.theme` ファイル読込で値を差し替えられる
///
/// @details `NS::Game::Theme::Get(id)` は 5 件の `ThemeData` への const 参照を返し、 範囲外は Grass
/// にフォールバックする 静的 storage 上に並ぶので参照は frame 越しに有効、 再読込で中身だけ変わる

#include <cstdint>
#include <filesystem>

#include "Game/Theme/ThemeData.h"
#include "Game/Theme/ThemeId.h"

namespace NS::Game::Theme
{
    /// 5 テーマの中から id に対応する ThemeData を返す
    /// Count 以上の範囲外なら入力境界の防御として ThemeId::Grass にフォールバックする
    [[nodiscard]] const ThemeData& Get(ThemeId id) noexcept;

    /// uint16_t の LevelData::themeId からの取得。 範囲外は Grass にフォールバックする
    [[nodiscard]] const ThemeData& Get(std::uint16_t levelDataThemeId) noexcept;

    /// ディレクトリの `.theme` 5 件を ThemeId と 1:1 の固定名で読み、 registry を上書きする
    /// 呼ぶ度に組み込み既定値へ戻してから読むため、 欠落・破損・値不正のテーマは既定値のままになる
    /// 未知キーは無視する。 起動列と editor のテーマ再読込が呼ぶ
    void LoadThemesFromDirectory(const std::filesystem::path& directory);
} // namespace NS::Game::Theme
