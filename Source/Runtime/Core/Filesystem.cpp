#include "Runtime/Core/Filesystem.h"

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

#include <windows.h>

namespace NS::Core
{

    bool FileSystem::Exists(const std::filesystem::path& path) noexcept
    {
        std::error_code ec;
        const bool result = std::filesystem::exists(path, ec);
        if (ec)
        {
            NS_LOG_ERROR(Core, "FileSystem::Exists failed: {} ({})", path.string(), ec.message());
            return false;
        }
        return result;
    }

    bool FileSystem::IsDirectory(const std::filesystem::path& path) noexcept
    {
        std::error_code ec;
        const bool result = std::filesystem::is_directory(path, ec);
        if (ec)
        {
            NS_LOG_ERROR(Core, "FileSystem::IsDirectory failed: {} ({})", path.string(), ec.message());
            return false;
        }
        return result;
    }

    std::optional<std::vector<std::byte>> FileSystem::ReadAllBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            NS_LOG_ERROR(Core, "FileSystem::ReadAllBytes failed to open: {}", path.string());
            return std::nullopt;
        }

        // サイズ取得
        const auto end = stream.tellg();
        if (end < 0)
        {
            NS_LOG_ERROR(Core, "FileSystem::ReadAllBytes failed to query size: {}", path.string());
            return std::nullopt;
        }
        stream.seekg(0, std::ios::beg);

        // 確保と読み込み
        try
        {
            std::vector<std::byte> buffer(static_cast<std::size_t>(end));
            if (end > 0)
            {
                stream.read(reinterpret_cast<char*>(buffer.data()), end);
                if (!stream)
                {
                    NS_LOG_ERROR(Core, "FileSystem::ReadAllBytes read failed: {}", path.string());
                    return std::nullopt;
                }
            }
            return buffer;
        }
        catch (const std::bad_alloc&)
        {
            NS_LOG_ERROR(Core,
                         "FileSystem::ReadAllBytes allocation failed: {} ({} bytes)",
                         path.string(),
                         static_cast<std::size_t>(end));
            return std::nullopt;
        }
    }

    bool FileSystem::WriteAllBytes(const std::filesystem::path& path, std::span<const std::byte> bytes)
    {
        // parent が空 path となるファイル名のみ指定の場合はスキップ
        const auto parent = path.parent_path();
        if (!parent.empty())
        {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec)
            {
                NS_LOG_ERROR(Core,
                             "FileSystem::WriteAllBytes failed to create directories: {} ({})",
                             parent.string(),
                             ec.message());
                return false;
            }
        }

        // 開いて書き込み
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            NS_LOG_ERROR(Core, "FileSystem::WriteAllBytes failed to open: {}", path.string());
            return false;
        }
        if (!bytes.empty())
        {
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!stream)
            {
                NS_LOG_ERROR(Core, "FileSystem::WriteAllBytes write failed: {}", path.string());
                return false;
            }
        }
        return true;
    }

    std::optional<std::string> FileSystem::ReadAllText(const std::filesystem::path& path)
    {
        std::ifstream stream(path);
        if (!stream)
        {
            NS_LOG_ERROR(Core, "FileSystem::ReadAllText failed to open: {}", path.string());
            return std::nullopt;
        }
        std::ostringstream oss;
        oss << stream.rdbuf();
        if (stream.bad())
        {
            NS_LOG_ERROR(Core, "FileSystem::ReadAllText read failed: {}", path.string());
            return std::nullopt;
        }
        return oss.str();
    }

    bool FileSystem::CreateDirectories(const std::filesystem::path& path) noexcept
    {
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        if (ec)
        {
            NS_LOG_ERROR(Core, "FileSystem::CreateDirectories failed: {} ({})", path.string(), ec.message());
            return false;
        }
        return true;
    }

    namespace
    {
        // 拡張子は ASCII の大文字小文字を区別せず比べる。Windows のファイルシステムの扱いに合わせる
        // wide→narrow の文字コード変換は変換不能文字で例外を投げ得るので、native のまま比べる
        [[nodiscard]] bool ExtensionMatches(const std::filesystem::path& file, std::string_view extension) noexcept
        {
            if (extension.empty())
                return true;
            // extension() の戻りは一時なので値で受ける。native() の参照だけ残すと宙づりになる
            const std::filesystem::path extPath = file.extension();
            const std::filesystem::path::string_type& ext = extPath.native();
            if (ext.size() != extension.size())
                return false;
            for (std::size_t i = 0; i < ext.size(); ++i)
            {
                const wchar_t wc = ext[i];
                if (wc > 127)
                    return false; // 比較対象の拡張子は ASCII 前提
                const int a = std::tolower(static_cast<int>(wc));
                const int b = std::tolower(static_cast<unsigned char>(extension[i]));
                if (a != b)
                    return false;
            }
            return true;
        }

        // directory_iterator / recursive_directory_iterator 共通の走査。開けない dir は空を返す
        template <typename Iterator>
        [[nodiscard]] std::vector<std::filesystem::path> CollectFiles(const std::filesystem::path& dir,
                                                                      std::string_view extension,
                                                                      const char* callerName)
        {
            std::vector<std::filesystem::path> result;

            std::error_code ec;
            Iterator it(dir, ec);
            if (ec)
            {
                NS_LOG_ERROR(Core, "{} failed to open: {} ({})", callerName, dir.string(), ec.message());
                return result;
            }

            const Iterator end;
            for (; it != end; it.increment(ec))
            {
                std::error_code entryEc;
                if (!it->is_regular_file(entryEc) || entryEc)
                    continue;
                if (!ExtensionMatches(it->path(), extension))
                    continue;
                result.push_back(it->path());
            }
            // increment の失敗はイテレータを end にするので、エラーはループを抜けてから確認する
            if (ec)
                NS_LOG_ERROR(Core, "{} iteration failed: {} ({})", callerName, dir.string(), ec.message());
            return result;
        }
    } // namespace

    std::vector<std::filesystem::path> FileSystem::ListFiles(const std::filesystem::path& dir,
                                                             std::string_view extension)
    {
        return CollectFiles<std::filesystem::directory_iterator>(dir, extension, "FileSystem::ListFiles");
    }

    std::vector<std::filesystem::path> FileSystem::ListFilesRecursive(const std::filesystem::path& dir,
                                                                      std::string_view extension)
    {
        return CollectFiles<std::filesystem::recursive_directory_iterator>(
            dir, extension, "FileSystem::ListFilesRecursive");
    }

    std::vector<std::filesystem::path> FileSystem::ListDirectories(const std::filesystem::path& dir)
    {
        std::vector<std::filesystem::path> result;

        std::error_code ec;
        std::filesystem::directory_iterator it(dir, ec);
        if (ec)
        {
            NS_LOG_ERROR(Core, "FileSystem::ListDirectories failed to open: {} ({})", dir.string(), ec.message());
            return result;
        }

        const std::filesystem::directory_iterator end;
        for (; it != end; it.increment(ec))
        {
            std::error_code entryEc;
            if (!it->is_directory(entryEc) || entryEc)
                continue;
            result.push_back(it->path());
        }
        // increment の失敗はイテレータを end にするので、エラーはループを抜けてから確認する
        if (ec)
            NS_LOG_ERROR(Core, "FileSystem::ListDirectories iteration failed: {} ({})", dir.string(), ec.message());
        return result;
    }

    std::filesystem::path FileSystem::GetExeDirectory()
    {
        std::array<wchar_t, MAX_PATH> buffer{};
        const DWORD len = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0 || len == buffer.size())
        {
            NS_LOG_ERROR(Core, "FileSystem::GetExeDirectory failed (GetLastError={})", ::GetLastError());
            return {};
        }
        return std::filesystem::path(std::wstring(buffer.data(), len)).parent_path();
    }

    std::filesystem::path FileSystem::ContentRoot()
    {
#if defined(NS_SHIPPING)
        return GetExeDirectory();
#else
        // exe から premake5.lua / .git を上位へ辿りリポジトリルートを返す
        for (auto dir = GetExeDirectory(); !dir.empty();)
        {
            std::error_code ec;
            if (std::filesystem::exists(dir / "premake5.lua", ec) || std::filesystem::exists(dir / ".git", ec))
                return dir;
            const auto parent = dir.parent_path();
            if (parent == dir)
                break;
            dir = parent;
        }
        return GetExeDirectory();
#endif
    }

    std::optional<std::filesystem::path> FileSystem::ResolveUnder(const std::filesystem::path& base,
                                                                  const std::filesystem::path& relative)
    {
        // 絶対パスとドライブ相対は base 配下を保証できないので入口で拒否する
        if (relative.is_absolute() || relative.has_root_name())
            return std::nullopt;
        std::filesystem::path combined = (base / relative).lexically_normal();
        // 正規化後も base 配下かを相対化で確かめる。外へ出ていれば先頭要素が ".." になる
        const std::filesystem::path fromBase = combined.lexically_relative(base.lexically_normal());
        if (fromBase.empty() || *fromBase.begin() == "..")
            return std::nullopt;
        return combined;
    }

} // namespace NS::Core
