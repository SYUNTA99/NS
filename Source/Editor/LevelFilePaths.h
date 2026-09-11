#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NS::Editor
{
    //! @brief レベル名がパス操作として安全かどうかを検証し、正規化する
    //! @details ASCII 英数字と '_'・'-'・空白だけを通し、".." による上位への移動と予約名を弾く
    //! @return 検証に失敗した場合は空文字列を返す
    [[nodiscard]] std::string SanitizeLevelName(std::string_view name) noexcept;

    //! @brief Assets からの相対パスを検証・正規化する。区切りは '/'
    //! @details 各セグメントを SanitizeLevelName で検証し、'..'・絶対パス・'\'・空セグメントを排除する
    //! @return 検証に失敗した場合は空文字列を返す
    [[nodiscard]] std::string SanitizeLevelPath(std::string_view relativePath) noexcept;

    //! @brief レベルデータの置き場を絶対パスで取得する
    [[nodiscard]] std::filesystem::path GetScenesDirectory() noexcept;

    //! @brief 区切りの無い素の名前を Scenes/ 配下の識別子に直す
    //! @details 保存と起動読込が同じファイルを指すよう、素の名前の置き場を Scenes/ に固定する
    //! @return 区切り付きの相対パスと空文字列はそのまま返す
    [[nodiscard]] std::string QualifyLevelPath(std::string_view sanitizedPath) noexcept;

    //! @brief レベル名から Assets/<相対パス>.scene のフルパスを組む
    //! @details 区切りの無い素の名前は Scenes/ 配下として解決する
    //! @return 構築に失敗した場合は std::nullopt を返す
    [[nodiscard]] std::optional<std::filesystem::path> BuildLevelPath(std::string_view name) noexcept;

    //! @brief レベル保存用ディレクトリが存在することを確認し、なければ作成する
    //! @return 作成に失敗した場合は false を返す
    [[nodiscard]] bool EnsureScenesDirectoryExists() noexcept;

    //! @brief 置き場の *.scene を全部探してファイル名を並べる
    //! @return ファイル名のリスト。列挙中に問題が発生した場合はその時点でのリストを返す
    [[nodiscard]] std::vector<std::string> EnumerateLevelFiles() noexcept;
} // namespace NS::Editor