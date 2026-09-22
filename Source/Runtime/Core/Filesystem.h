#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace NS::Core
{

    //! @brief ファイル・ディレクトリ操作の最小ユーティリティ
    //! @details 失敗時はエラーログを出力して nullopt や false を返す
    class FileSystem
    {
    public:
        FileSystem() = delete;

        //! 指定パスが存在する場合は true を返す
        [[nodiscard]] static bool Exists(std::string_view path) noexcept;

        //! 指定パスが存在しディレクトリの場合は true を返す
        [[nodiscard]] static bool IsDirectory(std::string_view path) noexcept;

        //! 空ファイルの場合は空の配列を返す
        [[nodiscard]] static std::optional<std::vector<std::byte>> ReadAllBytes(std::string_view path);

        //! 既存のファイルは上書きされ、存在しない中間ディレクトリは自動的に作成される
        [[nodiscard]] static bool WriteAllBytes(std::string_view path, std::span<const std::byte> bytes);

        //! 文字コードは UTF-8 を想定している
        [[nodiscard]] static std::optional<std::string> ReadAllText(std::string_view path);

        //! 中間ディレクトリも含めて作成する。すでに存在する場合も成功扱いとなる
        //! @note Win32 API のマクロとの名前衝突を避けるため複数形にしている
        [[nodiscard]] static bool CreateDirectories(std::string_view path) noexcept;

        //! extension に先頭ドット込みの拡張子を指定して絞り込みができる。大文字小文字は区別しない
        //! 戻り値の並び順は未規定
        [[nodiscard]] static std::vector<std::string> ListFiles(std::string_view dir, std::string_view extension = {});

        //! ListFiles の再帰版。サブディレクトリも辿る。戻り値の並び順は未規定
        [[nodiscard]] static std::vector<std::string> ListFilesRecursive(std::string_view dir,
                                                                         std::string_view extension = {});

        //! 戻り値の並び順は保証しない。ディレクトリが存在しない場合は空の配列を返す
        [[nodiscard]] static std::vector<std::string> ListDirectories(std::string_view dir);

        //! 実行ファイルが置かれているディレクトリの絶対パスを返す
        [[nodiscard]] static std::string GetExeDirectory();

        //! @brief アセットやシェーダなど、ランタイムコンテンツの基準ディレクトリ
        //! @details
        //! 出荷用ビルドでは実行ファイルと同階層を返す
        //! 開発環境ではリポジトリのルートディレクトリを探索して返すため、ビルド毎のファイル転送なしで開発アセットを直接読み込める
        //! ルートの探索に失敗した場合は実行ファイルと同階層にフォールバックする
        [[nodiscard]] static std::string ContentRoot();

        //! @brief base ディレクトリ配下に収まる相対パスのみを繋ぎ、正規化して返す
        //! @details 絶対パスやドライブ指定、または「..」などで階層を遡った結果として base
        //! の外へ出るパスが渡された場合は nullopt を返す
        //! プロジェクト外の不正なファイル読み込みを防ぐためのガードとして使用する
        [[nodiscard]] static std::optional<std::string> ResolveUnder(std::string_view base, std::string_view relative);

        //! base と relative を区切り 1 つで繋ぐ。base の末尾が区切りの場合は足さない
        //! どちらかが空の場合は残った方をそのまま返す
        //! relative がルートやドライブ指定で始まる場合は base を捨てて relative を返す
        [[nodiscard]] static std::string Combine(std::string_view base, std::string_view relative);

        //! 区切りを / へ統一し "." と ".." を畳んで返す
        [[nodiscard]] static std::string Normalize(std::string_view path);

        //! 最後の区切りより後ろの拡張子を、先頭のドット込みで返す。拡張子が無い場合は空文字列を返す
        //! 先頭のドットと ".." は拡張子の区切りとみなさない
        [[nodiscard]] static std::string Extension(std::string_view path);

        //! 最後の区切りより後ろを返す。区切りが無い場合は path 全体を返す
        [[nodiscard]] static std::string FileName(std::string_view path);

        //! 最後の区切りより後ろから、拡張子を除いた部分を返す
        [[nodiscard]] static std::string Stem(std::string_view path);

        //! 最後の区切りより前を返す。区切りが無い場合は空文字列を返す
        //! ルート直下の場合は "/" や "C:/" のように区切りまで残す
        [[nodiscard]] static std::string ParentDirectory(std::string_view path);
    };

} // namespace NS::Core
