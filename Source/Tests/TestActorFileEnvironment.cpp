// テストの全部が走る前に、.actor の置き場をテストの一時ディレクトリへ揃える。
// Assets/Actors の写しに、.actor の無い登録クラスの分を足し、全部の項目を生成する。
// テストのクラスも .actor から作るので、テストごとに .actor を手で書かずに済む

#include "Editor/ActorFileGenerator.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Windows/StringUtils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <system_error>

namespace
{
    class ActorFileEnvironment final : public ::testing::Environment
    {
    public:
        void SetUp() override
        {
            using NS::OS::FileSystem;
            const std::string root = FileSystem::ContentRoot();
            m_target = FileSystem::Combine(FileSystem::Combine(root, "build"), "TestActorFilesOfRun");
            const std::filesystem::path directory{NS::OS::StringUtils::WideFromUtf8(m_target)};
            std::error_code error;
            std::filesystem::remove_all(directory, error);
            std::filesystem::create_directories(directory, error);
            ASSERT_FALSE(error) << m_target << " を作れない";

            const std::string assets = FileSystem::Combine(FileSystem::Combine(root, "Assets"), "Actors");
            for (const std::string& path : FileSystem::ListFiles(assets, ".actor"))
            {
                const std::filesystem::path source{NS::OS::StringUtils::WideFromUtf8(path)};
                std::filesystem::copy_file(source, directory / source.filename(), error);
                ASSERT_FALSE(error) << path << " を写せない";
            }
            for (const NS::TypeRegistry::Entry& entry : NS::TypeRegistry::Get().Entries())
            {
                const std::string path = FileSystem::Combine(m_target, std::string{entry.className} + ".actor");
                if (FileSystem::Exists(path))
                {
                    continue;
                }
                nlohmann::json actorFile = nlohmann::json::object();
                NS::SetActorFileClass(actorFile, entry.className);
                ASSERT_TRUE(NS::WriteActorFileAt(path, actorFile));
            }
            (void)NS::Editor::GenerateActorFiles(m_target);
            NS::ActorFileLibrary::Get().SetDirectory(m_target);
        }

        void TearDown() override
        {
            std::error_code error;
            std::filesystem::remove_all(std::filesystem::path{NS::OS::StringUtils::WideFromUtf8(m_target)}, error);
        }

    private:
        std::string m_target;
    };

    // gtest の約束で、登録した Environment は gtest が持って壊す
    const ::testing::Environment* const k_ActorFileEnvironment =
        ::testing::AddGlobalTestEnvironment(new ActorFileEnvironment);
} // namespace
