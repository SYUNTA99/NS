#include <ns/core/string_utils.h>

#include <ns/core/log_categories.h>
#include <ns/core/logger.h>

#include <windows.h>

namespace ns::core
{

    std::wstring StringUtils::WideFromUtf8(std::string_view utf8)
    {
        if (utf8.empty())
        {
            return {};
        }

        const int srcLen = static_cast<int>(utf8.size());
        const int dstLen = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), srcLen, nullptr, 0);
        if (dstLen <= 0)
        {
            NS_LOG_ERROR(LogCat::Core, "StringUtils::WideFromUtf8 failed (GetLastError={})", ::GetLastError());
            return {};
        }

        std::wstring result(static_cast<std::size_t>(dstLen), L'\0');
        const int written =
            ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), srcLen, result.data(), dstLen);
        if (written <= 0)
        {
            NS_LOG_ERROR(LogCat::Core, "StringUtils::WideFromUtf8 conversion failed");
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

        const int srcLen = static_cast<int>(wide.size());
        const int dstLen =
            ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), srcLen, nullptr, 0, nullptr, nullptr);
        if (dstLen <= 0)
        {
            NS_LOG_ERROR(LogCat::Core, "StringUtils::Utf8FromWide failed (GetLastError={})", ::GetLastError());
            return {};
        }

        std::string result(static_cast<std::size_t>(dstLen), '\0');
        const int written = ::WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), srcLen, result.data(), dstLen, nullptr, nullptr);
        if (written <= 0)
        {
            NS_LOG_ERROR(LogCat::Core, "StringUtils::Utf8FromWide conversion failed");
            return {};
        }
        return result;
    }

} // namespace ns::core
