#pragma once

#include <string>
#include <string_view>

namespace NS::Core
{

    /// 内部用のUTF-8と、Win32 API等のOS境界で要求されるワイド文字列(UTF-16)を相互変換する。
    /// 変換失敗時は例外を投げず、エラーログを出力して空文字列を返す。
    class StringUtils
    {
    public:
        StringUtils() = delete;

        /// char の UTF-8 を wchar_t の UTF-16 へ変換
        [[nodiscard]] static std::wstring WideFromUtf8(std::string_view utf8);

        /// wchar_t の UTF-16 を char の UTF-8 へ変換
        [[nodiscard]] static std::string Utf8FromWide(std::wstring_view wide);
    };

} // namespace NS::Core
