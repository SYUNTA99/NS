#include "Editor/LevelFilePaths.h"


#include <algorithm>
#include <array>
#include <cctype>

namespace NS::Editor
{
    namespace
    {
        constexpr std::size_t kMinNameLen = 1;
        constexpr std::size_t kMaxNameLen = 200;

        // CON / PRN / AUX / NUL / COM1-9 / LPT1-9。 拡張子付きでも CON.txt 等が reject される
        // のが Windows API の振る舞いに準じた安全側設定
        constexpr std::array<std::string_view, 22> kReservedNames = {
            "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
            "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};

        [[nodiscard]] bool IsReservedName(std::string_view name) noexcept
        {
            std::string upper;
            upper.reserve(name.size());
            for (char c : name)
                upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            for (const auto& r : kReservedNames)
            {
                if (upper == r)
                    return true;
            }
            return false;
        }
    } // namespace

    std::string SanitizeLevelName(std::string_view name) noexcept
    {
        if (name.size() < kMinNameLen || name.size() > kMaxNameLen)
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

    std::filesystem::path GetLevelsDirectory() noexcept
    {
        return NS::Core::FileSystem::GetExeDirectory() / "Levels";
    }

    std::optional<std::filesystem::path> BuildLevelPath(std::string_view name) noexcept
    {
        const auto safe = SanitizeLevelName(name);
        if (safe.empty())
            return std::nullopt;
        return GetLevelsDirectory() / (safe + ".scene");
    }

    bool EnsureLevelsDirectoryExists() noexcept
    {
        const auto dir = GetLevelsDirectory();
        if (NS::Core::FileSystem::Exists(dir))
            return true;
        if (!NS::Core::FileSystem::CreateDirectories(dir))
        {
            NS_LOG_ERROR(::NS::Core::LogCat::App, "Levels/ ディレクトリ作成失敗");
            return false;
        }
        return true;
    }

    std::vector<std::string> EnumerateLevelFiles() noexcept
    {
        std::vector<std::string> result;
        const auto dir = GetLevelsDirectory();
        if (!NS::Core::FileSystem::Exists(dir))
            return result;

        for (const auto& path : NS::Core::FileSystem::ListFiles(dir, ".scene"))
        {
            auto stem = path.stem().string();
            if (!SanitizeLevelName(stem).empty())
                result.push_back(std::move(stem));
        }
        std::sort(result.begin(), result.end());
        return result;
    }

} // namespace NS::Editor
