#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NS::Editor
{
    //! @brief レベル名がパス操作として安全かどうかを検証し、正規化する
    //! @details ASCII英数字、'_'、'-'、空白のみを許可し、パス移動（".."）や予約名を排除する
    //! @return 検証に失敗した場合は空文字列を返す
    [[nodiscard]] std::string SanitizeLevelName(std::string_view name) noexcept;

    //! @brief Assets からの相対パス（'/' 区切り）を検証・正規化する
    //! @details 各セグメントを SanitizeLevelName で検証し、'..'・絶対パス・'\'・空セグメントを排除する
    //! @return 検証に失敗した場合は空文字列を返す
    [[nodiscard]] std::string SanitizeLevelPath(std::string_view relativePath) noexcept;

    //! @brief レベルデータの格納ディレクトリ（絶対パス）を取得する
    [[nodiscard]] std::filesystem::path GetScenesDirectory() noexcept;

    //! @brief 指定されたレベル名から、フルパス（Scenes/<name>.scene）を構築する
    //! @return 構築に失敗した場合は std::nullopt を返す
    [[nodiscard]] std::optional<std::filesystem::path> BuildLevelPath(std::string_view name) noexcept;

    //! @brief レベル保存用ディレクトリが存在することを確認し、なければ作成する
    //! @return 作成に失敗した場合は false を返す
    [[nodiscard]] bool EnsureScenesDirectoryExists() noexcept;

    //! @brief ディレクトリ内のすべてのレベルファイル（*.scene）を検索し、ファイル名をリストアップする
    //! @return ファイル名のリスト。列挙中に問題が発生した場合はその時点でのリストを返す
    [[nodiscard]] std::vector<std::string> EnumerateLevelFiles() noexcept;
} // namespace NS::Editor