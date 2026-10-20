#pragma once

#include "NSlib/CoreObject/Reflection/Archetype.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/StringUtils.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

// 種類の既定値の置き場を試しごとの空のディレクトリへ向け、終わったら元へ戻す
// 前の実行が残したファイルは消してから始める
class ScopedArchetypeDirectory
{
public:
    explicit ScopedArchetypeDirectory(std::string_view name) : m_previous(NS::ArchetypeLibrary::Get().Directory())
    {
        using NS::OS::FileSystem;
        m_directory = FileSystem::Combine(
            FileSystem::Combine(FileSystem::Combine(FileSystem::ContentRoot(), "build"), "TestArchetypes"), name);
        (void)FileSystem::CreateDirectories(m_directory);
        for (const std::string& path : FileSystem::ListFiles(m_directory, ".json"))
        {
            std::error_code error;
            std::filesystem::remove(std::filesystem::path{NS::OS::StringUtils::WideFromUtf8(path)}, error);
        }
        NS::ArchetypeLibrary::Get().SetDirectory(m_directory);
    }

    ~ScopedArchetypeDirectory() { NS::ArchetypeLibrary::Get().SetDirectory(m_previous); }

    ScopedArchetypeDirectory(const ScopedArchetypeDirectory&) = delete;
    ScopedArchetypeDirectory& operator=(const ScopedArchetypeDirectory&) = delete;

    [[nodiscard]] const std::string& Path() const noexcept { return m_directory; }

private:
    std::string m_previous;
    std::string m_directory;
};
