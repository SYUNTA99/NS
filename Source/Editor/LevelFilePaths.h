#pragma once

/// @file LevelFilePaths.h
/// @brief `Levels/` ディレクトリ管理 + level name sanitization
///
/// @details ImGui save dialog の入力をそのまま `std::filesystem::path` に流すと
/// `..` / 絶対 path / Windows 予約名 経由で exe 外への書込ができてしまう
/// 本ヘッダの `SanitizeLevelName` を必ず経由させることで、 path traversal 経路を
/// 構造的に閉じる。 file 名構築は `BuildLevelPath` のみが正規ルートで、
/// 内部で sanitize した name しか accept しない


namespace NS::Editor
{
    /// ASCII 英数字+`_`/`-`/空白、1〜200 char、`..`なし、Windows 予約名なし。 失敗時は空文字列
    [[nodiscard]] std::string SanitizeLevelName(std::string_view name) noexcept;

    /// `<exe>/Levels/` の絶対 path。 exe 起動 directory に依存
    [[nodiscard]] std::filesystem::path GetLevelsDirectory() noexcept;

    /// sanitize 済 name から `<exe>/Levels/<name>.scene` を構築。 失敗時は `std::nullopt`
    [[nodiscard]] std::optional<std::filesystem::path> BuildLevelPath(std::string_view name) noexcept;

    /// `Levels/` を必要なら作成。 既存なら何もしない。 作成失敗時は false + `NS_LOG_ERROR`
    [[nodiscard]] bool EnsureLevelsDirectoryExists() noexcept;

    /// `<exe>/Levels/*.scene` を列挙しソート済 stem を返す。 例外時は部分リスト + `NS_LOG_ERROR`
    [[nodiscard]] std::vector<std::string> EnumerateLevelFiles() noexcept;
} // namespace NS::Editor
