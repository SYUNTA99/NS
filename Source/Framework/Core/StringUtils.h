#pragma once

#include <string>
#include <string_view>

namespace NS::Core
{

    /// UTF-8 と UTF-16 (wide) 間の変換ユーティリティ。
    /// 不正入力時はエラーログを出力して空文字列を返す (例外は投げない)。
    class StringUtils
    {
    public:
        StringUtils() = delete;

        /// UTF-8 (char) → UTF-16 (wchar_t) 変換
        [[nodiscard]] static std::wstring WideFromUtf8(std::string_view utf8);

        /// UTF-16 (wchar_t) → UTF-8 (char) 変換
        [[nodiscard]] static std::string Utf8FromWide(std::wstring_view wide);
    };

} // namespace NS::Core
