#pragma once

/// @file Filesystem.h
/// @brief NS::Core::FileSystem — ファイル / ディレクトリ操作の最小ユーティリティ。
///
/// @details 全 API が `std::optional` の `nullopt` / `bool false` で失敗を返し、
/// 例外は外に伝播させない (失敗時は内部で `NS_LOG_ERROR` を出す)。
/// 戻り値は呼出側が必ず確認することを前提とするため `[[nodiscard]]` 必須。

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace NS::Core
{

    /// ファイル / ディレクトリ操作の最小ユーティリティ。
    /// 例外を投げず、失敗は std::optional の nullopt / bool false で表現する。
    class FileSystem
    {
    public:
        FileSystem() = delete;

        /// `path` が存在するか
        [[nodiscard]] static bool Exists(const std::filesystem::path& path);

        /// `path` のバイナリ内容を読み込む。失敗時 nullopt + NS_LOG_ERROR。空ファイルは空 vector を返す
        [[nodiscard]] static std::optional<std::vector<std::byte>> ReadAllBytes(const std::filesystem::path& path);

        /// `path` のテキスト内容を読み込む (UTF-8 想定)。失敗時 nullopt + NS_LOG_ERROR
        [[nodiscard]] static std::optional<std::string> ReadAllText(const std::filesystem::path& path);

        /// `path` のディレクトリを (必要なら中間も) 作成する。既存も true。失敗時 false + NS_LOG_ERROR。
        /// Win32 `<windows.h>` の `CreateDirectory` マクロと衝突するため複数形を採用。
        /// 戻り値の確認漏れは sandbox 構築失敗の見落としに直結するため `[[nodiscard]]`。
        [[nodiscard]] static bool CreateDirectories(const std::filesystem::path& path);

        /// 実行ファイルが置かれているディレクトリの絶対パス
        [[nodiscard]] static std::filesystem::path GetExeDirectory();
    };

} // namespace NS::Core
