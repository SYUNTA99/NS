#pragma once

#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/Windows/FileSystem.h"
#include "NSlib/Windows/StringUtils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

//! .actor の置き場を試しごとの空のディレクトリへ向け、終わったら元へ戻す
//! 前の実行が残したファイルは消してから始める
class ScopedActorFileDirectory
{
public:
    explicit ScopedActorFileDirectory(std::string_view name) : m_previous(NS::ActorFileLibrary::Get().Directory())
    {
        using NS::OS::FileSystem;
        m_directory = FileSystem::Combine(
            FileSystem::Combine(FileSystem::Combine(FileSystem::ContentRoot(), "build"), "TestActorFiles"), name);
        (void)FileSystem::CreateDirectories(m_directory);
        for (const std::string& path : FileSystem::ListFiles(m_directory, ".actor"))
        {
            std::error_code error;
            std::filesystem::remove(std::filesystem::path{NS::OS::StringUtils::WideFromUtf8(path)}, error);
        }
        NS::ActorFileLibrary::Get().SetDirectory(m_directory);
    }

    ~ScopedActorFileDirectory() { NS::ActorFileLibrary::Get().SetDirectory(m_previous); }

    ScopedActorFileDirectory(const ScopedActorFileDirectory&) = delete;
    ScopedActorFileDirectory& operator=(const ScopedActorFileDirectory&) = delete;

    [[nodiscard]] const std::string& Path() const noexcept { return m_directory; }

private:
    std::string m_previous;
    std::string m_directory;
};

//! directory/name.actor へ text をそのまま書く
inline void WriteActorFile(const std::string& directory, std::string_view name, const std::string& text)
{
    const std::string path = NS::OS::FileSystem::Combine(directory, std::string{name} + ".actor");
    ASSERT_TRUE(
        NS::OS::FileSystem::WriteAllBytes(path, std::as_bytes(std::span<const char>{text.data(), text.size()})));
}

//! 中身がクラスだけの .actor の文書。欄の値はコードの既定のまま
inline nlohmann::json ClassOnlyActorFile(std::string_view className)
{
    nlohmann::json actorFile = nlohmann::json::object();
    NS::SetActorFileClass(actorFile, className);
    return actorFile;
}

//! @brief 試しの間だけ name の .actor を読んだ物として敷き、壊す時に外す
//! @details ファイルへは書かない。Assets/Actors に .actor の無い試しのクラスを配置物から作る時に使う
//! 置き場を差し替える型と使う時は、置き場を差し替えた後に作る。差し替えは読み直すので、前に敷いた物が消える
class ScopedActorFile
{
public:
    ScopedActorFile(std::string_view name, nlohmann::json actorFile) : m_name(name)
    {
        NS::ActorFileLibrary::Get().Set(m_name, std::move(actorFile));
    }

    ~ScopedActorFile() { NS::ActorFileLibrary::Get().Erase(m_name); }

    ScopedActorFile(const ScopedActorFile&) = delete;
    ScopedActorFile& operator=(const ScopedActorFile&) = delete;

private:
    std::string m_name;
};
