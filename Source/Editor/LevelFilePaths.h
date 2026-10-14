#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NS::Editor
{
    //! @brief Assets からの相対パスを検証・正規化する。区切りは '/'
    //! @details 各セグメントを SanitizeLevelName で検証し、'..'・絶対パス・'\'・空セグメントを排除する
    //! @return 検証に失敗した場合は空文字列を返す
    [[nodiscard]] std::string SanitizeLevelPath(std::string_view relativePath) noexcept;

    //! 区切りのない名前を Scenes/ 配下へ直す。区切り付きのパスと空文字列はそのまま
    [[nodiscard]] std::string QualifyLevelPath(std::string_view sanitizedPath) noexcept;

    //! Assets 配下の .scene パスを作る。素の名前は Scenes/ 配下、無効な名前は nullopt
    [[nodiscard]] std::optional<std::string> BuildLevelPath(std::string_view name) noexcept;

    //! @brief レベル保存用ディレクトリが存在することを確認し、なければ作成する
    //! @return 作成に失敗した場合は false を返す
    [[nodiscard]] bool EnsureScenesDirectoryExists() noexcept;

    //! Assets 配下の有効な .scene を相対名の辞書順で返す。直下のファイルと無効な名前は除外する
    [[nodiscard]] std::vector<std::string> EnumerateLevelFiles() noexcept;
} // namespace NS::Editor
