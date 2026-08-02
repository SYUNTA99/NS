#include "Editor/LevelFilePaths.h"

#include "Runtime/Core/Filesystem.h"
#include "Runtime/Core/Logger.h"

#include <array>
#include <cctype>

namespace NS::Editor
{
    namespace
    {
        constexpr std::size_t k_MinNameLen = 1;
        constexpr std::size_t k_MaxNameLen = 200;

        constexpr std::array<std::string_view, 22> k_ReservedNames = {
            "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
            "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};

        [[nodiscard]] bool IsReservedName(std::string_view name) noexcept
        {
            std::string upper;
            upper.reserve(name.size());
            for (char c : name)
                upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            for (const auto& r : k_ReservedNames)
            {
                if (upper == r)
                    return true;
            }
            return false;
        }
    } // namespace

    std::string SanitizeLevelName(std::string_view name) noexcept
    {
        if (name.size() < k_MinNameLen || name.size() > k_MaxNameLen)
            return "";
        for (char c : name)
        {
            const bool isAlnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
            const bool isAllowedPunct = (c == '_' || c == '-' || c == ' ');
            if (!isAlnum && !isAllowedPunct)
                return "";
        }
        if (name.find("..") != std::string_view::npos)
            return "";
        if (name.front() == ' ' || name.back() == ' ')
            return "";
        if (IsReservedName(name))
            return "";
        return std::string{name};
    }

    std::string SanitizeLevelPath(std::string_view relativePath) noexcept
    {
        if (relativePath.empty())
            return "";

        std::string result;
        std::size_t start = 0;
        for (;;)
        {
            const std::size_t slash = relativePath.find('/', start);
            const std::size_t end = (slash == std::string_view::npos) ? relativePath.size() : slash;
            const std::string_view segment = relativePath.substr(start, end - start);
            if (SanitizeLevelName(segment).empty())
                return "";
            if (!result.empty())
                result.push_back('/');
            result.append(segment);
            if (slash == std::string_view::npos)
                break;
            start = slash + 1;
        }
        return result;
    }

    std::filesystem::path GetScenesDirectory() noexcept
    {
        return NS::Core::FileSystem::ContentRoot() / "Assets" / "Scenes";
    }

    std::optional<std::filesystem::path> BuildLevelPath(std::string_view name) noexcept
    {
        const auto safe = SanitizeLevelPath(name);
        if (safe.empty())
            return std::nullopt;
        return NS::Core::FileSystem::ResolveUnder(NS::Core::FileSystem::ContentRoot() / "Assets", safe + ".scene");
    }

    bool EnsureScenesDirectoryExists() noexcept
    {
        const auto dir = GetScenesDirectory();
        if (NS::Core::FileSystem::Exists(dir))
            return true;
        if (!NS::Core::FileSystem::CreateDirectories(dir))
        {
            NS_LOG_ERROR(App, "Scenes/ ディレクトリ作成失敗");
            return false;
        }
        return true;
    }

    std::vector<std::string> EnumerateLevelFiles() noexcept
    {
        std::vector<std::string> result;
        const auto assetsDir = NS::Core::FileSystem::ContentRoot() / "Assets";
        if (!NS::Core::FileSystem::Exists(assetsDir))
            return result;

        for (const auto& path : NS::Core::FileSystem::ListFilesRecursive(assetsDir, ".scene"))
        {
            auto rel = path.lexically_relative(assetsDir);
            rel.replace_extension();
            std::string relStr = rel.generic_string();
            if (!SanitizeLevelPath(relStr).empty())
                result.push_back(std::move(relStr));
        }
        std::sort(result.begin(), result.end());
        return result;
    }

} // namespace NS::Editor
