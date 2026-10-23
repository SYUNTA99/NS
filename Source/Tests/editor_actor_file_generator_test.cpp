#include "Editor/ActorFileGenerator.h"
#include "NSlib/CoreObject/ObjectJson.h"
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
} // namespace

// 書いてある項目は値のまま残し、書いていない項目をコードの値で足す。根の姿勢と参照の欄は書かない
TEST(ActorFileGenerator, CompletesMissingFieldsFromTheCodeAndKeepsWrittenValues)
{
    nlohmann::json actorFile = ClassOnlyActorFile("PropActor");
    NS::ObjectJsonClassObjs(actorFile)["Params"] = {{"摩擦", 0.25f}};
    NS::ObjectJsonClassObjs(actorFile)["Transform"] = {{NS::k_PositionFieldName, {1.0f, 2.0f, 3.0f}}};

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
    NS::ObjectJsonClassObjs(derived)["Params"] = {{"質量", 2.0f}};
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
