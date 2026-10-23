#pragma once

#include "Editor/ActorFileGenerator.h"
#include "NSlib/CoreObject/ActorInitInfo.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Windows/FileSystem.h"
#include "NSlib/Windows/StringUtils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

//! @brief .actor の置き場を試しごとのディレクトリへ向け、終わったら元へ戻す
//! @details 前の置き場の .actor を写してから始めるので、登録した全部のクラスの .actor がある。
//! 前の実行が残したファイルは消してから写す
class ScopedActorFileDirectory
{
public:
    explicit ScopedActorFileDirectory(std::string_view name) : m_previous(NS::ActorFileLibrary::Get().Directory())
    {
        using NS::OS::FileSystem;
        m_directory = FileSystem::Combine(
            FileSystem::Combine(FileSystem::Combine(FileSystem::ContentRoot(), "build"), "TestActorFiles"), name);
        (void)FileSystem::CreateDirectories(m_directory);
        const std::filesystem::path directory{NS::OS::StringUtils::WideFromUtf8(m_directory)};
        for (const std::string& path : FileSystem::ListFiles(m_directory, ".actor"))
        {
            std::error_code error;
            std::filesystem::remove(std::filesystem::path{NS::OS::StringUtils::WideFromUtf8(path)}, error);
        }
        for (const std::string& path : FileSystem::ListFiles(m_previous, ".actor"))
        {
            const std::filesystem::path source{NS::OS::StringUtils::WideFromUtf8(path)};
            std::error_code error;
            std::filesystem::copy_file(source, directory / source.filename(), error);
            EXPECT_FALSE(error) << path;
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

//! directory/name.actor を消す。ライブラリは読み直さない
inline void RemoveActorFile(const std::string& directory, std::string_view name)
{
    const std::string path = NS::OS::FileSystem::Combine(directory, std::string{name} + ".actor");
    std::error_code error;
    std::filesystem::remove(std::filesystem::path{NS::OS::StringUtils::WideFromUtf8(path)}, error);
    ASSERT_FALSE(error) << path;
}

//! @brief directory の派生の .actor ("base" を持つ物) を全部消す。ライブラリは読み直さない
//! @details 元の .actor を壊すと、それを元にする派生も ERROR で読まないので、元の ERROR だけを数える時に消す
inline void RemoveDerivedActorFiles(const std::string& directory)
{
    for (const std::string& path : NS::OS::FileSystem::ListFiles(directory, ".actor"))
    {
        const std::optional<std::string> text = NS::OS::FileSystem::ReadAllText(path);
        ASSERT_TRUE(text.has_value()) << path;
        const nlohmann::json actorFile = nlohmann::json::parse(*text, nullptr, false);
        if (NS::ActorFileBase(actorFile).empty())
        {
            continue;
        }
        std::error_code error;
        std::filesystem::remove(std::filesystem::path{NS::OS::StringUtils::WideFromUtf8(path)}, error);
        ASSERT_FALSE(error) << path;
    }
}

//! 中身がクラスだけの .actor の文書。欄の値はコードの既定のまま
inline nlohmann::json ClassOnlyActorFile(std::string_view className)
{
    nlohmann::json actorFile = nlohmann::json::object();
    NS::SetActorFileClass(actorFile, className);
    return actorFile;
}

//! className の登録した全部の項目をコードの値で書いた .actor の文書。ライブラリへ入れる文書はこの形
inline nlohmann::json CodeValuesActorFile(std::string_view className)
{
    std::vector<std::string> added;
    return NS::Editor::CompleteActorFile(ClassOnlyActorFile(className), added);
}

//! name の .actor の全部の値で、シーンに入れずに作ったアクター。.actor が読まれていなければ nullptr
[[nodiscard]] inline std::unique_ptr<NS::SceneActor> ActorFromFile(std::string_view name)
{
    return NS::CreateActorOfClass(NS::ActorFileLibrary::Get().ClassNameOf(name), NS::ActorInitInfo{.actorFile = name});
}

//! @brief 試しの間だけ name の .actor を読んだ物として敷き、壊す時に前の文書へ戻す
//! @details ファイルへは書かない。前に文書が無ければ外す。
//! 置き場を差し替える型と使う時は、置き場を差し替えた後に作る。差し替えは読み直すので、前に敷いた物が消える
class ScopedActorFile
{
public:
    ScopedActorFile(std::string_view name, nlohmann::json actorFile) : m_name(name)
    {
        if (const nlohmann::json* previous = NS::ActorFileLibrary::Get().Find(m_name))
        {
            m_previous = *previous;
        }
        EXPECT_TRUE(NS::ActorFileLibrary::Get().Set(m_name, std::move(actorFile))) << m_name;
    }

    ~ScopedActorFile()
    {
        if (m_previous.has_value())
        {
            (void)NS::ActorFileLibrary::Get().Set(m_name, std::move(*m_previous));
            return;
        }
        NS::ActorFileLibrary::Get().Erase(m_name);
    }

    ScopedActorFile(const ScopedActorFile&) = delete;
    ScopedActorFile& operator=(const ScopedActorFile&) = delete;

private:
    std::string m_name;
    std::optional<nlohmann::json> m_previous;
};

//! @brief 試しの間だけ、読んである name の .actor を edit で書き替えた物に替え、壊す時に元へ戻す
//! @details 出荷の .actor の一部だけを変えて試す時に使う。アクターは .actor を作る時に読むので、シーンに置く前に作る。
//! 元が無い場合と、書き替えた物を断られた場合は試しを落とす
class ScopedActorFileEdit
{
public:
    ScopedActorFileEdit(std::string_view name, const std::function<void(nlohmann::json&)>& edit) : m_name(name)
    {
        const nlohmann::json* original = NS::ActorFileLibrary::Get().Find(m_name);
        EXPECT_NE(original, nullptr) << m_name;
        if (original == nullptr)
        {
            return;
        }
        m_original = *original;
        nlohmann::json edited = m_original;
        edit(edited);
        m_replaced = NS::ActorFileLibrary::Get().Set(m_name, std::move(edited));
        EXPECT_TRUE(m_replaced) << m_name;
    }

    ~ScopedActorFileEdit()
    {
        if (m_replaced)
        {
            (void)NS::ActorFileLibrary::Get().Set(m_name, std::move(m_original));
        }
    }

    ScopedActorFileEdit(const ScopedActorFileEdit&) = delete;
    ScopedActorFileEdit& operator=(const ScopedActorFileEdit&) = delete;

private:
    std::string m_name;
    nlohmann::json m_original;
    bool m_replaced = false;
};
