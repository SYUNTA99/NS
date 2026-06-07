#pragma once

/// @file LevelFilePaths.h
/// @brief `Levels/` ディレクトリ管理 + level name sanitization
///
/// @details ImGui save dialog の入力をそのまま `std::filesystem::path` に流すと
/// `..` / 絶対 path / Windows 予約名 経由で exe 外への書込ができてしまう
/// 本ヘッダの `SanitizeLevelName` を必ず経由させることで、 path traversal 経路を
/// 構造的に閉じる。 file 名構築は `BuildLevelPath` のみが正規ルートで、
/// 内部で sanitize した name しか accept しない

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NS::Game::Editor
{
    /// 安全な level name のみ通す。 許可: ASCII 英数字 + `_` / `-` / 半角空白、
    /// 1〜200 char、 `..` 含まず、 先頭末尾は空白以外、 Windows 予約名でないこと
    /// 失敗時は空 string を返す (no-throw、 `[[nodiscard]]`)
    [[nodiscard]] std::string SanitizeLevelName(std::string_view name) noexcept;

    /// `<exe>/Levels/` の絶対 path。 exe 起動 directory に依存
    [[nodiscard]] std::filesystem::path GetLevelsDirectory() noexcept;

    /// sanitize 済 name から `<exe>/Levels/<name>.nslvl` を構築
    /// sanitize 失敗時は `std::nullopt`。 caller は `*path` を直に I/O に渡せる
    [[nodiscard]] std::optional<std::filesystem::path> BuildLevelPath(std::string_view name) noexcept;

    /// `Levels/` を必要なら作成。 既存なら何もしない。 作成失敗時は false + `NS_LOG_ERROR`
    [[nodiscard]] bool EnsureLevelsDirectoryExists() noexcept;

    /// `<exe>/Levels/*.nslvl` を列挙し、 拡張子を除いた sanitize 済 stem を返す
    /// サブディレクトリは対象外。 ソート済 ASCII 比較。 例外発生時は部分的な list を返し
    /// `NS_LOG_ERROR` を出す
    [[nodiscard]] std::vector<std::string> EnumerateLevelFiles() noexcept;
} // namespace NS::Game::Editor
