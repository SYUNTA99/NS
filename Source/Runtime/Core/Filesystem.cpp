#include "Runtime/Core/Filesystem.h"

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Core/StringUtils.h"

#include <array>
#include <cctype>
#include <format>
#include <string>
#include <string_view>

#include <windows.h>

namespace NS::Core
{

    namespace
    {
        class FileHandle
        {
        public:
            explicit FileHandle(HANDLE handle) noexcept : m_handle(handle) {}

            ~FileHandle()
            {
                if (m_handle != INVALID_HANDLE_VALUE)
                {
                    ::CloseHandle(m_handle);
                }
            }

            FileHandle(const FileHandle&) = delete;
            FileHandle& operator=(const FileHandle&) = delete;

            [[nodiscard]] bool IsValid() const noexcept { return m_handle != INVALID_HANDLE_VALUE; }
            [[nodiscard]] HANDLE Get() const noexcept { return m_handle; }

        private:
            HANDLE m_handle;
        };

        class FindHandle
        {
        public:
            explicit FindHandle(HANDLE handle) noexcept : m_handle(handle) {}

            ~FindHandle()
            {
                if (m_handle != INVALID_HANDLE_VALUE)
                {
                    ::FindClose(m_handle);
                }
            }

            FindHandle(const FindHandle&) = delete;
            FindHandle& operator=(const FindHandle&) = delete;

            [[nodiscard]] bool IsValid() const noexcept { return m_handle != INVALID_HANDLE_VALUE; }
            [[nodiscard]] HANDLE Get() const noexcept { return m_handle; }

        private:
            HANDLE m_handle;
        };

        // 番号だけではログに失敗の内容が残らない
        [[nodiscard]] std::string ErrorText(DWORD error)
        {
            wchar_t* buffer = nullptr;
            const DWORD len = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                                               nullptr,
                                               error,
                                               MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                               reinterpret_cast<wchar_t*>(&buffer),
                                               0,
                                               nullptr);
            if (len == 0 || buffer == nullptr)
            {
                return std::format("{}", error);
            }

            std::wstring_view wide(buffer, len);
            while (!wide.empty() && (wide.back() == L'\r' || wide.back() == L'\n'))
            {
                wide.remove_suffix(1);
            }
            const std::string text = StringUtils::Utf8FromWide(wide);
            ::LocalFree(buffer);
            return std::format("{}: {}", error, text);
        }

        // TODO: 260 文字を超えるパスは失敗する。扱うなら \\?\ プレフィックスを付ける
        [[nodiscard]] DWORD AttributesOf(std::string_view path) noexcept
        {
            return ::GetFileAttributesW(StringUtils::WideFromUtf8(path).c_str());
        }
    } // namespace

    bool FileSystem::Exists(std::string_view path) noexcept
    {
        return AttributesOf(path) != INVALID_FILE_ATTRIBUTES;
    }

    bool FileSystem::IsDirectory(std::string_view path) noexcept
    {
        const DWORD attributes = AttributesOf(path);
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            return false;
        }
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    std::optional<std::vector<std::byte>> FileSystem::ReadAllBytes(std::string_view path)
    {
        const FileHandle handle(::CreateFileW(StringUtils::WideFromUtf8(path).c_str(),
                                              GENERIC_READ,
                                              FILE_SHARE_READ,
                                              nullptr,
                                              OPEN_EXISTING,
                                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                              nullptr));
        if (!handle.IsValid())
        {
            NS_LOG_ERROR(Core, "FileSystem::ReadAllBytes failed to open: {} ({})", path, ErrorText(::GetLastError()));
            return std::nullopt;
        }

        LARGE_INTEGER size{};
        if (::GetFileSizeEx(handle.Get(), &size) == 0)
        {
            NS_LOG_ERROR(
                Core, "FileSystem::ReadAllBytes failed to query size: {} ({})", path, ErrorText(::GetLastError()));
            return std::nullopt;
        }

        std::vector<std::byte> buffer(static_cast<std::size_t>(size.QuadPart));
        if (size.QuadPart > 0)
        {
            DWORD read = 0;
            if (::ReadFile(handle.Get(), buffer.data(), static_cast<DWORD>(size.QuadPart), &read, nullptr) == 0)
            {
                NS_LOG_ERROR(Core, "FileSystem::ReadAllBytes read failed: {} ({})", path, ErrorText(::GetLastError()));
                return std::nullopt;
            }
            if (read != static_cast<DWORD>(size.QuadPart))
            {
                NS_LOG_ERROR(
                    Core, "FileSystem::ReadAllBytes read short: {} ({} / {} bytes)", path, read, size.QuadPart);
                return std::nullopt;
            }
        }
        return buffer;
    }

    bool FileSystem::WriteAllBytes(std::string_view path, std::span<const std::byte> bytes)
    {
        // ファイル名のみ指定なら親が空になるので掘らない
        const std::string parent = ParentDirectory(path);
        if (!parent.empty())
        {
            if (!CreateDirectories(parent))
            {
                NS_LOG_ERROR(Core, "FileSystem::WriteAllBytes failed to create directories: {}", parent);
                return false;
            }
        }

        const FileHandle handle(::CreateFileW(StringUtils::WideFromUtf8(path).c_str(),
                                              GENERIC_WRITE,
                                              0,
                                              nullptr,
                                              CREATE_ALWAYS,
                                              FILE_ATTRIBUTE_NORMAL,
                                              nullptr));
        if (!handle.IsValid())
        {
            NS_LOG_ERROR(Core, "FileSystem::WriteAllBytes failed to open: {} ({})", path, ErrorText(::GetLastError()));
            return false;
        }

        if (!bytes.empty())
        {
            DWORD written = 0;
            if (::WriteFile(handle.Get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) == 0)
            {
                NS_LOG_ERROR(
                    Core, "FileSystem::WriteAllBytes write failed: {} ({})", path, ErrorText(::GetLastError()));
                return false;
            }
            if (written != static_cast<DWORD>(bytes.size()))
            {
                NS_LOG_ERROR(
                    Core, "FileSystem::WriteAllBytes write short: {} ({} / {} bytes)", path, written, bytes.size());
                return false;
            }
        }
        return true;
    }

    std::optional<std::string> FileSystem::ReadAllText(std::string_view path)
    {
        const auto bytes = ReadAllBytes(path);
        if (!bytes.has_value())
        {
            return std::nullopt;
        }

        // バイナリで読むので改行が \r\n のまま来る。テキストとして返す前に畳む
        std::string text;
        text.reserve(bytes->size());
        for (std::size_t i = 0; i < bytes->size(); ++i)
        {
            const char c = static_cast<char>((*bytes)[i]);
            if (c == '\r' && i + 1 < bytes->size() && static_cast<char>((*bytes)[i + 1]) == '\n')
            {
                continue;
            }
            text.push_back(c);
        }
        return text;
    }

    bool FileSystem::CreateDirectories(std::string_view path) noexcept
    {
        if (path.empty() || IsDirectory(path))
        {
            return true;
        }

        const std::string parent = ParentDirectory(path);
        if (!parent.empty() && parent != path && !CreateDirectories(parent))
        {
            return false;
        }

        if (::CreateDirectoryW(StringUtils::WideFromUtf8(path).c_str(), nullptr) != 0)
        {
            return true;
        }

        const DWORD error = ::GetLastError();
        if (error == ERROR_ALREADY_EXISTS)
        {
            return true;
        }
        NS_LOG_ERROR(Core, "FileSystem::CreateDirectories failed: {} ({})", path, ErrorText(error));
        return false;
    }

    namespace
    {
        // 拡張子は ASCII の大文字小文字を区別せず比べる。Windows のファイルシステムの扱いに合わせる
        [[nodiscard]] bool ExtensionMatches(std::string_view file, std::string_view extension) noexcept
        {
            if (extension.empty())
            {
                return true;
            }
            const std::string ext = FileSystem::Extension(file);
            if (ext.size() != extension.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < ext.size(); ++i)
            {
                const unsigned char c = static_cast<unsigned char>(ext[i]);
                if (c > 127)
                {
                    return false;
                }

                const int a = std::tolower(static_cast<int>(c));
                const int b = std::tolower(static_cast<unsigned char>(extension[i]));
                if (a != b)
                {
                    return false;
                }
            }
            return true;
        }

        template <typename OnEntry> void ForEachEntry(std::string_view dir, const char* callerName, OnEntry&& onEntry)
        {
            WIN32_FIND_DATAW data{};
            const std::wstring pattern = StringUtils::WideFromUtf8(FileSystem::Combine(dir, "*"));
            const FindHandle handle(::FindFirstFileW(pattern.c_str(), &data));
            if (!handle.IsValid())
            {
                NS_LOG_ERROR(Core, "{} failed to open: {} ({})", callerName, dir, ErrorText(::GetLastError()));
                return;
            }

            do
            {
                const std::wstring_view name = data.cFileName;
                if (name == L"." || name == L"..")
                {
                    continue;
                }
                onEntry(data.dwFileAttributes, FileSystem::Combine(dir, StringUtils::Utf8FromWide(name)));
            }
            while (::FindNextFileW(handle.Get(), &data) != 0);

            const DWORD error = ::GetLastError();
            if (error != ERROR_NO_MORE_FILES)
            {
                NS_LOG_ERROR(Core, "{} iteration failed: {} ({})", callerName, dir, ErrorText(error));
            }
        }
    } // namespace

    std::vector<std::string> FileSystem::ListFiles(std::string_view dir, std::string_view extension)
    {
        std::vector<std::string> result;
        ForEachEntry(dir, "FileSystem::ListFiles", [&](DWORD attributes, std::string entry) {
            if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                return;
            }
            if (!ExtensionMatches(entry, extension))
            {
                return;
            }
            result.push_back(std::move(entry));
        });
        return result;
    }

    std::vector<std::string> FileSystem::ListFilesRecursive(std::string_view dir, std::string_view extension)
    {
        std::vector<std::string> result;
        std::vector<std::string> pending{std::string(dir)};

        while (!pending.empty())
        {
            const std::string current = std::move(pending.back());
            pending.pop_back();

            ForEachEntry(current, "FileSystem::ListFilesRecursive", [&](DWORD attributes, std::string entry) {
                if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                {
                    pending.push_back(std::move(entry));
                    return;
                }
                if (!ExtensionMatches(entry, extension))
                {
                    return;
                }
                result.push_back(std::move(entry));
            });
        }
        return result;
    }

    std::vector<std::string> FileSystem::ListDirectories(std::string_view dir)
    {
        std::vector<std::string> result;
        ForEachEntry(dir, "FileSystem::ListDirectories", [&](DWORD attributes, std::string entry) {
            if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                result.push_back(std::move(entry));
            }
        });
        return result;
    }

    std::string FileSystem::GetExeDirectory()
    {
        std::array<wchar_t, MAX_PATH> buffer{};
        const DWORD len = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0 || len == buffer.size())
        {
            NS_LOG_ERROR(Core, "FileSystem::GetExeDirectory failed ({})", ErrorText(::GetLastError()));
            return {};
        }

        return ParentDirectory(StringUtils::Utf8FromWide(std::wstring_view(buffer.data(), len)));
    }

    std::string FileSystem::ContentRoot()
    {
#if defined(NS_SHIPPING)
        return GetExeDirectory();
#else
        // exe から premake5.lua / .git を上位へ辿りリポジトリルートを返す
        for (std::string dir = GetExeDirectory(); !dir.empty();)
        {
            if (Exists(Combine(dir, "premake5.lua")) || Exists(Combine(dir, ".git")))
            {
                return dir;
            }
            std::string parent = ParentDirectory(dir);
            if (parent == dir)
            {
                break;
            }
            dir = std::move(parent);
        }

        return GetExeDirectory();
#endif
    }

    namespace
    {
        [[nodiscard]] bool HasDriveLetter(std::string_view path) noexcept
        {
            return path.size() >= 2 && path[1] == ':';
        }

        [[nodiscard]] bool TakesOverBase(std::string_view base, std::string_view relative) noexcept
        {
            if (relative.empty())
            {
                return false;
            }
            if (relative.front() == '/' || relative.front() == '\\')
            {
                return true;
            }
            if (!HasDriveLetter(relative))
            {
                return false;
            }
            // C:x を捨てると operator/ の C:/NS/x と食い違うので、区切りの有無で絶対と分ける
            if (relative.size() >= 3 && (relative[2] == '/' || relative[2] == '\\'))
            {
                return true;
            }
            if (HasDriveLetter(base))
            {
                return std::tolower(static_cast<unsigned char>(base[0])) !=
                       std::tolower(static_cast<unsigned char>(relative[0]));
            }
            return true;
        }

        [[nodiscard]] std::size_t RootLength(std::string_view path) noexcept
        {
            if (path.size() >= 2 && path[1] == ':')
            {
                return (path.size() >= 3 && (path[2] == '/' || path[2] == '\\')) ? 3 : 2;
            }
            if (!path.empty() && (path.front() == '/' || path.front() == '\\'))
            {
                return 1;
            }
            return 0;
        }

    } // namespace

    std::string FileSystem::Normalize(std::string_view path)
    {
        std::string slashed(path);
        for (char& c : slashed)
        {
            if (c == '\\')
            {
                c = '/';
            }
        }

        const std::size_t rootLen = RootLength(slashed);
        const std::string_view body(slashed);

        std::vector<std::string_view> parts;
        std::size_t i = rootLen;
        while (i <= body.size())
        {
            const std::size_t next = body.find('/', i);
            const std::size_t end = (next == std::string_view::npos) ? body.size() : next;
            const std::string_view part = body.substr(i, end - i);

            if (part == "..")
            {
                // ルートが無いパスでは先頭の .. を畳めないので、そのまま残さないと外への脱出を見落とす
                if (!parts.empty() && parts.back() != "..")
                {
                    parts.pop_back();
                }
                else if (rootLen == 0)
                {
                    parts.push_back(part);
                }
            }
            else if (!part.empty() && part != ".")
            {
                parts.push_back(part);
            }

            if (next == std::string_view::npos)
            {
                break;
            }
            i = next + 1;
        }

        std::string result = slashed.substr(0, rootLen);
        for (const std::string_view part : parts)
        {
            if (!result.empty() && result.back() != '/')
            {
                result.push_back('/');
            }
            result.append(part);
        }
        return result;
    }

    namespace
    {
        [[nodiscard]] std::string_view FileNameView(std::string_view path) noexcept
        {
            const std::size_t pos = path.find_last_of("/\\");
            if (pos == std::string_view::npos)
            {
                return path;
            }
            return path.substr(pos + 1);
        }

        [[nodiscard]] std::size_t ExtensionStart(std::string_view name) noexcept
        {
            if (name == "." || name == "..")
            {
                return std::string_view::npos;
            }
            const std::size_t dot = name.rfind('.');
            // 標準の extension() は先頭のドットを拡張子と見ないので 0 は外す
            if (dot == 0)
            {
                return std::string_view::npos;
            }
            return dot;
        }
    } // namespace

    std::string FileSystem::Combine(std::string_view base, std::string_view relative)
    {
        if (base.empty())
        {
            return std::string(relative);
        }
        if (TakesOverBase(base, relative))
        {
            return std::string(relative);
        }

        std::string combined(base);
        const char last = combined.back();
        if (last != '/' && last != '\\')
        {
            combined.push_back('/');
        }
        // C: を残すと C:/NS/C:x になるので落として繋ぐ
        if (HasDriveLetter(relative))
        {
            combined.append(relative.substr(2));
        }
        else
        {
            combined.append(relative);
        }
        return combined;
    }

    std::string FileSystem::Extension(std::string_view path)
    {
        const std::string_view name = FileNameView(path);
        const std::size_t dot = ExtensionStart(name);
        if (dot == std::string_view::npos)
        {
            return {};
        }
        return std::string(name.substr(dot));
    }

    std::string FileSystem::FileName(std::string_view path)
    {
        return std::string(FileNameView(path));
    }

    std::string FileSystem::Stem(std::string_view path)
    {
        const std::string_view name = FileNameView(path);
        const std::size_t dot = ExtensionStart(name);
        if (dot == std::string_view::npos)
        {
            return std::string(name);
        }
        return std::string(name.substr(0, dot));
    }

    std::string FileSystem::ParentDirectory(std::string_view path)
    {
        const std::size_t pos = path.find_last_of("/\\");
        if (pos == std::string_view::npos)
        {
            return {};
        }
        // ルート直下は区切りまで残す。C: まで削るとドライブ相対になり別の場所を指す
        if (pos == 0)
        {
            return std::string(path.substr(0, 1));
        }
        if (pos == 2 && path[1] == ':')
        {
            return std::string(path.substr(0, 3));
        }
        return std::string(path.substr(0, pos));
    }

    std::optional<std::string> FileSystem::ResolveUnder(std::string_view base, std::string_view relative)
    {
        // 絶対パスとドライブ相対は base 配下に収まる保証が無いので、畳む前に落とす
        if (relative.empty())
        {
            return std::nullopt;
        }
        if (relative.front() == '/' || relative.front() == '\\')
        {
            return std::nullopt;
        }
        if (HasDriveLetter(relative))
        {
            return std::nullopt;
        }

        const std::string baseNorm = Normalize(base);
        const std::string combined = Normalize(Combine(base, relative));

        // 畳んだ後に base の外へ出ていないかを接頭辞で見る。base 自身も配下ではないので長さで外す
        if (combined.size() <= baseNorm.size())
        {
            return std::nullopt;
        }
        if (::_strnicmp(combined.c_str(), baseNorm.c_str(), baseNorm.size()) != 0)
        {
            return std::nullopt;
        }
        // 区切りを見ないと Assets の判定が AssetsEvil を通す
        if (combined[baseNorm.size()] != '/')
        {
            return std::nullopt;
        }

        return combined;
    }

} // namespace NS::Core
