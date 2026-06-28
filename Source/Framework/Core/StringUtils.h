#pragma once

/// @file StringUtils.h
/// @brief NS::Core::StringUtils — UTF-8 と wide な UTF-16 の間の変換ユーティリティ
///
/// @details `CreateWindowExW` 等の Win32 API は wide 文字列を要求するため、
/// プロジェクト内のテキストは UTF-8 で持ち、 OS 境界で wide に変換する方針
/// 不正入力時は `NS_LOG_ERROR` を出して空文字列を返し例外は投げない

#include <string>
#include <string_view>

namespace NS::Core
{

    /// UTF-8 と wide な UTF-16 の間の変換ユーティリティ。不正入力時は空文字列 + NS_LOG_ERROR
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
