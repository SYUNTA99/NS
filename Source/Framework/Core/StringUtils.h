#pragma once

/// @file StringUtils.h
/// @brief NS::Core::StringUtils — UTF-8 と UTF-16 (wide) 間の変換ユーティリティ
///
/// @details Win32 API (`CreateWindowExW` 等) は wide 文字列を要求するため、
/// プロジェクト内のテキストは UTF-8 で持ち、 OS 境界で wide に変換する方針
/// 不正入力時は `NS_LOG_ERROR` を出して空文字列を返す (例外は投げない)

#include <string>
#include <string_view>

namespace NS::Core
{

    /// UTF-8 と UTF-16 (wide) 間の変換ユーティリティ。不正入力時は空文字列 + NS_LOG_ERROR
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
