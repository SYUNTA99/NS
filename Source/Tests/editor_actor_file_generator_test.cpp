#include "Editor/ActorFileGenerator.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Windows/Filesystem.h"
#include "TestActorFile.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    bool Contains(const std::vector<std::string>& added, const std::string& item)
    {
        return std::find(added.begin(), added.end(), item) != added.end();
    }

    // .actor に手で書いた 0.43 は、float に読むと 0.4300000071525574 で書き出される。数は float で比べる
    bool SameFieldValue(const nlohmann::json& read, const nlohmann::json& written)
    {
        if (read.is_number() && written.is_number())
        {
            return read.get<float>() == written.get<float>();
        }
        if (read.is_array() && written.is_array())
        {
            if (read.size() != written.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < read.size(); ++i)
            {
                if (!SameFieldValue(read[i], written[i]))
                {
                    return false;
                }
            }
            return true;
        }
        return read == written;
    }
} // namespace

// 書いてある項目は値のまま残し、書いていない項目をコードの値で足す。根の姿勢と参照の欄は書かない
TEST(ActorFileGenerator, CompletesMissingFieldsFromTheCodeAndKeepsWrittenValues)
{
    nlohmann::json actorFile = ClassOnlyActorFile("PropActor");
    NS::ActorJsonClassObjs(actorFile)["Params"] = {{"摩擦", 0.25f}};
    NS::ActorJsonClassObjs(actorFile)["Transform"] = {{NS::k_PositionFieldName, {1.0f, 2.0f, 3.0f}}};

    std::vector<std::string> added;
    const nlohmann::json full = NS::Editor::CompleteActorFile(actorFile, added);

    EXPECT_EQ(NS::ActorFileClass(full), "PropActor");
    const nlohmann::json* params = NS::ClassObjFields(full, "Params");
    ASSERT_NE(params, nullptr);
    EXPECT_FLOAT_EQ(params->at("摩擦").get<float>(), 0.25f);
    EXPECT_EQ(NS::ClassObjFields(full, "Transform"), nullptr);
    EXPECT_TRUE(Contains(added, "Params/跳ね返り"));
    EXPECT_FALSE(Contains(added, "Params/摩擦"));

    // 根の他の部品の、参照でない欄は全部ある
    const std::unique_ptr<NS::SceneActor> rock = NS::CreateActorWithCodeValues("PropActor");
    ASSERT_NE(rock, nullptr);
    int checked = 0;
    NS::ForEachReflectedClassObj(*rock, [&](const NS::ClassObjInfo& row, const NS::Object* part) {
        if (part == &rock->Root())
        {
            return;
        }
        const nlohmann::json* written = NS::ClassObjFields(full, row.key);
        ASSERT_NE(written, nullptr) << row.key;
        const nlohmann::json code = NS::SerializeClassObjFields(*part);
        for (nlohmann::json::const_iterator field = code.begin(); field != code.end(); ++field)
        {
            EXPECT_TRUE(written->contains(field.key())) << row.key << " " << field.key();
            ++checked;
        }
    });
    EXPECT_GT(checked, 0);
}

TEST(ActorFileGenerator, LeavesOutReferenceFields)
{
    std::vector<std::string> added;
    const nlohmann::json full = NS::Editor::CompleteActorFile(ClassOnlyActorFile("FollowCameraActor"), added);
    const nlohmann::json* vcam = NS::ClassObjFields(full, "Vcam");
    ASSERT_NE(vcam, nullptr);
    EXPECT_FALSE(vcam->contains("追従対象"));
    EXPECT_FALSE(vcam->empty());
}

// 派生の .actor は元から受け継ぐので書き足さない
TEST(ActorFileGenerator, LeavesTheDerivedActorFileAsWritten)
{
    nlohmann::json derived = ClassOnlyActorFile("PropActor");
    NS::SetActorFileBase(derived, "PropActor");
    NS::ActorJsonClassObjs(derived)["Params"] = {{"質量", 2.0f}};
    std::vector<std::string> added;
    EXPECT_EQ(NS::Editor::CompleteActorFile(derived, added), derived);
    EXPECT_TRUE(added.empty());
}

// ディレクトリの .actor を書き足して書き戻す。2 回目は何も足さない
TEST(ActorFileGenerator, WritesTheCompletedActorFilesBackToTheDirectory)
{
    const ScopedActorFileDirectory scope("ActorFileGenerator");
    WriteActorFile(scope.Path(), "PropActor", R"({"class": "PropActor", "classObjs": {"Params": {"摩擦": 0.25}}})");

    EXPECT_EQ(NS::Editor::GenerateActorFiles(scope.Path()), 1);
    const std::optional<nlohmann::json> written =
        NS::ReadActorFileAt(NS::OS::FileSystem::Combine(scope.Path(), "PropActor.actor"));
    ASSERT_TRUE(written.has_value());
    const nlohmann::json* params = NS::ClassObjFields(*written, "Params");
    ASSERT_NE(params, nullptr);
    EXPECT_FLOAT_EQ(params->at("摩擦").get<float>(), 0.25f);
    EXPECT_TRUE(params->contains("跳ね返り"));

    EXPECT_EQ(NS::Editor::GenerateActorFiles(scope.Path()), 0);
}

// Assets/Actors の .actor は、そのクラスで登録した項目を全部書いている。項目を足したら生成で書き足す
TEST(ActorFileGenerator, EveryAssetActorFileHasEveryRegisteredField)
{
    const std::string directory = NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), "Actors");
    int checked = 0;
    for (const std::string& path : NS::OS::FileSystem::ListFiles(directory, ".actor"))
    {
        const std::optional<nlohmann::json> actorFile = NS::ReadActorFileAt(path);
        ASSERT_TRUE(actorFile.has_value()) << path;
        std::vector<std::string> added;
        (void)NS::Editor::CompleteActorFile(*actorFile, added);
        for (const std::string& item : added)
        {
            ADD_FAILURE() << path << " に項目 " << item << " が無い";
        }
        ++checked;
    }
    EXPECT_GE(checked, 7);
}

// Assets/Actors の .actor から作ったアクターは、根の他の全部の項目が .actor に書いた値になる。OnInit は項目へ値を書かない
// 元の .actor は全部の部品を書く。派生は書いた部品だけを持ち、残りは元から受け継ぐ
TEST(ActorFileGenerator, ActorsTakeEveryFieldFromTheAssetActorFile)
{
    const std::string directory = NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), "Actors");
    int checked = 0;
    for (const std::string& path : NS::OS::FileSystem::ListFiles(directory, ".actor"))
    {
        const std::optional<nlohmann::json> actorFile = NS::ReadActorFileAt(path);
        ASSERT_TRUE(actorFile.has_value()) << path;
        const bool derived = !NS::ActorFileBase(*actorFile).empty();
        const std::unique_ptr<NS::SceneActor> actor = ActorFromFile(NS::OS::FileSystem::Stem(path));
        ASSERT_NE(actor, nullptr) << path;
        NS::ForEachReflectedClassObj(*actor, [&](const NS::ClassObjInfo& row, const NS::Object* part) {
            if (part == &actor->Root())
            {
                return;
            }
            const nlohmann::json* written = NS::ClassObjFields(*actorFile, row.key);
            if (written == nullptr)
            {
                EXPECT_TRUE(derived) << path << " " << row.key;
                return;
            }
            const nlohmann::json values = NS::SerializeClassObjFields(*part);
            for (nlohmann::json::const_iterator field = written->begin(); field != written->end(); ++field)
            {
                ASSERT_TRUE(values.contains(field.key())) << path << " " << row.key << " " << field.key();
                EXPECT_TRUE(SameFieldValue(values.at(field.key()), field.value()))
                    << path << " " << row.key << " " << field.key() << ": " << values.at(field.key()).dump() << " と "
                    << field.value().dump();
                ++checked;
            }
        });
    }
    EXPECT_GT(checked, 0);
}

// テストの起動は、登録した全部のクラスの .actor を一時ディレクトリに揃えてから走る
TEST(ActorFileGenerator, TestsRunWithAnActorFileForEveryRegisteredClass)
{
    const std::string assets = NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), "Actors");
    EXPECT_NE(NS::ActorFileLibrary::Get().Directory(), assets);
    for (const NS::TypeRegistry::Entry& entry : NS::TypeRegistry::Get().Entries())
    {
        EXPECT_EQ(NS::ActorFileLibrary::Get().ClassNameOf(entry.className), entry.className) << entry.className;
    }
}
