#pragma once

/// @file Filesystem.h
/// @brief NS::Core::FileSystem — ファイル / ディレクトリ操作の最小ユーティリティ
///
/// @details 全 API が `std::optional` の `nullopt` / `bool false` で失敗を返し、
/// 例外は外に伝播させない (失敗時は内部で `NS_LOG_ERROR` を出す)
/// 戻り値は呼出側が必ず確認することを前提とするため `[[nodiscard]]` 必須

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace NS::Core
{

    /// ファイル / ディレクトリ操作の最小ユーティリティ。失敗は nullopt / false で返す
    class FileSystem
    {
    public:
        FileSystem() = delete;

        /// `path` が存在するか
        [[nodiscard]] static bool Exists(const std::filesystem::path& path) noexcept;

        /// バイナリ読み込み。失敗時 nullopt + NS_LOG_ERROR。空ファイルは空 vector
        [[nodiscard]] static std::optional<std::vector<std::byte>> ReadAllBytes(const std::filesystem::path& path);

        /// バイナリ書き込み (上書き)。中間ディレクトリは自動作成。失敗時 false + NS_LOG_ERROR
        [[nodiscard]] static bool WriteAllBytes(const std::filesystem::path& path, std::span<const std::byte> bytes);

        /// テキスト読み込み (UTF-8 想定)。失敗時 nullopt + NS_LOG_ERROR
        [[nodiscard]] static std::optional<std::string> ReadAllText(const std::filesystem::path& path);

        /// ディレクトリを中間も含め作成する。既存でも true。Win32 マクロ衝突回避で複数形
        [[nodiscard]] static bool CreateDirectories(const std::filesystem::path& path) noexcept;

        /// `dir` 直下の通常ファイルを列挙する。`extension` で拡張子絞り込み可 (先頭ドット込み)
        /// 失敗時は空 vector + NS_LOG_ERROR。戻り順は未規定
        [[nodiscard]] static std::vector<std::filesystem::path> ListFiles(const std::filesystem::path& dir,
                                                                          std::string_view extension = {});

        /// `dir` 直下のサブディレクトリを列挙する。失敗時は空 vector + NS_LOG_ERROR。戻り順は未規定
        [[nodiscard]] static std::vector<std::filesystem::path> ListDirectories(const std::filesystem::path& dir);

        /// 実行ファイルが置かれているディレクトリの絶対パス
        [[nodiscard]] static std::filesystem::path GetExeDirectory();

        /// アセット / シェーダ等ランタイムコンテンツの基準ディレクトリ
        /// @details 出荷ビルド (`NS_SHIPPING`) は exe と同階層、 開発ビルドはリポジトリルート
        /// (`premake5.lua` / `.git` を上位探索) を返す。 これにより開発時はリポ直下の `Assets/`
        /// `Shaders/` を直接読み、 ビルド毎のコピーを不要にする。 ルート検出失敗時は exe 同階層へ fallback
        [[nodiscard]] static std::filesystem::path ContentRoot();
    };

} // namespace NS::Core
