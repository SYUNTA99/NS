#include "Runtime/Core/StringUtils.h"

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"

#include <limits>

#include <windows.h>

namespace NS::Core
{

    std::wstring StringUtils::WideFromUtf8(std::string_view utf8)
    {
        if (utf8.empty())
        {
            return {};
        }

        if (utf8.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            NS_LOG_ERROR(Core, "StringUtils::WideFromUtf8 input exceeds INT_MAX ({} bytes)", utf8.size());
            return {};
        }

        // 出力長の計測
        const int srcLen = static_cast<int>(utf8.size());
        const int dstLen = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), srcLen, nullptr, 0);
        if (dstLen <= 0)
        {
            NS_LOG_ERROR(Core, "StringUtils::WideFromUtf8 failed (GetLastError={})", ::GetLastError());
            return {};
        }

        // 確保して変換
        std::wstring result(static_cast<std::size_t>(dstLen), L'\0');
        const int written =
            ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), srcLen, result.data(), dstLen);
        if (written != dstLen)
        {
            NS_LOG_ERROR(
                Core, "StringUtils::WideFromUtf8 conversion incomplete (written={}, expected={})", written, dstLen);
            return {};
        }
        return result;
    }

    std::string StringUtils::Utf8FromWide(std::wstring_view wide)
    {
        if (wide.empty())
        {
            return {};
        }

        if (wide.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            NS_LOG_ERROR(Core, "StringUtils::Utf8FromWide input exceeds INT_MAX ({} chars)", wide.size());
            return {};
        }

        // 出力長の計測
        const int srcLen = static_cast<int>(wide.size());
        const int dstLen =
            ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), srcLen, nullptr, 0, nullptr, nullptr);
        if (dstLen <= 0)
        {
            NS_LOG_ERROR(Core, "StringUtils::Utf8FromWide failed (GetLastError={})", ::GetLastError());
            return {};
        }

        // 確保して変換
        std::string result(static_cast<std::size_t>(dstLen), '\0');
        const int written = ::WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), srcLen, result.data(), dstLen, nullptr, nullptr);
        if (written != dstLen)
        {
            NS_LOG_ERROR(
                Core, "StringUtils::Utf8FromWide conversion incomplete (written={}, expected={})", written, dstLen);
            return {};
        }
        return result;
    }

} // namespace NS::Core
