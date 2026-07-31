#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace NS::Core
{

    /// @brief ファイル・ディレクトリ操作の最小ユーティリティ
    /// @details 例外は外に伝播させず、失敗時はエラーログを出力して nullopt や false を返す
    class FileSystem
    {
    public:
        FileSystem() = delete;

        [[nodiscard]] static bool Exists(const std::filesystem::path& path) noexcept;

        [[nodiscard]] static bool IsDirectory(const std::filesystem::path& path) noexcept;

        /// 空ファイルの場合は空の配列を返す
        [[nodiscard]] static std::optional<std::vector<std::byte>> ReadAllBytes(const std::filesystem::path& path);

        /// 既存のファイルは上書きされ、存在しない中間ディレクトリは自動的に作成される
        [[nodiscard]] static bool WriteAllBytes(const std::filesystem::path& path, std::span<const std::byte> bytes);

        /// 文字コードは UTF-8 を想定している
        [[nodiscard]] static std::optional<std::string> ReadAllText(const std::filesystem::path& path);

        /// 中間ディレクトリも含めて作成する。すでに存在する場合も成功扱いとなる
        /// @note Win32 API のマクロとの名前衝突を避けるため複数形にしている
        [[nodiscard]] static bool CreateDirectories(const std::filesystem::path& path) noexcept;

        /// extension に先頭ドット込みの拡張子を指定して絞り込みができる。戻り値の並び順は未規定
        [[nodiscard]] static std::vector<std::filesystem::path> ListFiles(const std::filesystem::path& dir,
                                                                          std::string_view extension = {});

        /// 戻り値の並び順は保証しない。ディレクトリが存在しない場合は空の配列を返す
        [[nodiscard]] static std::vector<std::filesystem::path> ListDirectories(const std::filesystem::path& dir);

        /// 実行ファイルが置かれているディレクトリの絶対パスを返す
        [[nodiscard]] static std::filesystem::path GetExeDirectory();

        /// @brief アセットやシェーダなど、ランタイムコンテンツの基準ディレクトリ
        /// @details
        /// 出荷用ビルドでは実行ファイルと同階層を返す。
        /// 開発環境ではリポジトリのルートディレクトリを探索して返すため、ビルド毎のファイル転送なしで開発アセットを直接読み込める。
        /// ルートの探索に失敗した場合は実行ファイルと同階層にフォールバックする。
        [[nodiscard]] static std::filesystem::path ContentRoot();

        /// @brief base ディレクトリ配下に収まる相対パスのみを繋ぎ、正規化して返す
        /// @details 絶対パスやドライブ指定、または「..」などで階層を遡った結果として base
        /// の外へ出るパスが渡された場合は nullopt を返す。
        /// プロジェクト外の不正なファイル読み込みを防ぐためのガードとして使用する。
        [[nodiscard]] static std::optional<std::filesystem::path> ResolveUnder(const std::filesystem::path& base,
                                                                               const std::filesystem::path& relative);
    };

} // namespace NS::Core
