#include "Editor/Undo/AddObjectCommand.h"
#include "Editor/Undo/DuplicateObjectCommand.h"
#include "Editor/Undo/PlaceCommand.h"
#include "Framework/Scene/ObjectRef.h"
#include "Game/Level/EditTarget.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/LevelJson.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

namespace EditorNs = NS::Editor;
namespace LevelNs = NS::Game::Level;

namespace
{
    [[nodiscard]] bool AllIdsUniqueAndAssigned(const LevelNs::LevelData& level)
    {
        std::unordered_set<std::uint32_t> seen;
        for (const auto& object : level.objects)
        {
            if (object.objectId == 0 || !seen.insert(object.objectId).second)
                return false;
        }
        return true;
    }
} // namespace

TEST(ObjectIdTest, EnsureUniqueAssignsMissingIds)
{
    LevelNs::LevelData level;
    level.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 0));
    level.objects.push_back(LevelNs::MakeGridObject(1, 0, 0, 0));
    level.objects.push_back(LevelNs::MakeGridObject(2, 0, 0, 0));

    LevelNs::EnsureUniqueObjectIds(level);

    EXPECT_TRUE(AllIdsUniqueAndAssigned(level));
    // カウンタは既存最大 id の先を指す
    for (const auto& object : level.objects)
        EXPECT_LT(object.objectId, level.nextObjectId);
}

TEST(ObjectIdTest, EnsureUniqueReassignsDuplicatesKeepingFirst)
{
    LevelNs::LevelData level;
    level.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 0));
    level.objects.push_back(LevelNs::MakeGridObject(1, 0, 0, 0));
    level.objects.push_back(LevelNs::MakeGridObject(2, 0, 0, 0));
    level.objects[0].objectId = 5;
    level.objects[1].objectId = 5;
    level.objects[2].objectId = 2;

    LevelNs::EnsureUniqueObjectIds(level);

    // 先勝ちで最初の 5 は保持、2 番目に新 id、既存の 2 も保持
    EXPECT_EQ(level.objects[0].objectId, 5u);
    EXPECT_NE(level.objects[1].objectId, 5u);
    EXPECT_EQ(level.objects[2].objectId, 2u);
    EXPECT_TRUE(AllIdsUniqueAndAssigned(level));
}

TEST(ObjectIdTest, JsonRoundTripPreservesIdsAndCounter)
{
    LevelNs::LevelData level;
    level.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 0));
    level.objects.push_back(LevelNs::MakeGridObject(3, 1, 2, 1));
    level.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    // 追従カメラも積んでおく。 読込の門の合成で採番カウンタが動くと counter 比較が成り立たないため
    level.objects.push_back(LevelNs::MakeFollowCameraObject(0u));
    LevelNs::EnsureUniqueObjectIds(level);
    const std::uint32_t id0 = level.objects[0].objectId;
    const std::uint32_t id1 = level.objects[1].objectId;
    const std::uint32_t counter = level.nextObjectId;

    const std::string text = LevelNs::SerializeLevelToJson(level);
    LevelNs::LevelData restored;
    ASSERT_TRUE(LevelNs::DeserializeLevelFromJson(restored, text));

    ASSERT_EQ(restored.objects.size(), 4u);
    EXPECT_EQ(restored.objects[0].objectId, id0);
    EXPECT_EQ(restored.objects[1].objectId, id1);
    EXPECT_EQ(restored.nextObjectId, counter);
}

TEST(ObjectIdTest, LegacyJsonWithoutIdsGetsAssignedOnLoad)
{
    // v2 相当の最小 JSON。id と nextObjectId が無い旧ファイルを読むと採番される
    const std::string legacy = R"({
        "formatVersion": 2,
        "objects": [
            {"transform": {"pos": [0.0, 0.0, 0.0]}, "flags": 1, "components": []},
            {"transform": {"pos": [1.0, 0.0, 0.0]}, "flags": 1, "components": []}
        ]
    })";

    LevelNs::LevelData restored;
    ASSERT_TRUE(LevelNs::DeserializeLevelFromJson(restored, legacy));
    // 旧形式なのでプレイヤーと追従カメラも合成され 4 件になる。 合成分にも一意 id が振られる
    ASSERT_EQ(restored.objects.size(), 4u);
    EXPECT_TRUE(AllIdsUniqueAndAssigned(restored));
}

TEST(ObjectIdTest, FindObjectIndexByIdReturnsMatchingIndex)
{
    LevelNs::LevelData level;
    level.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 0));
    level.objects.push_back(LevelNs::MakeGridObject(1, 0, 0, 0));
    level.objects.push_back(LevelNs::MakeGridObject(2, 0, 0, 0));
    LevelNs::EnsureUniqueObjectIds(level);

    const std::uint32_t id = level.objects[1].objectId;
    EXPECT_EQ(LevelNs::FindObjectIndexById(level, id), 1u);
}

TEST(ObjectIdTest, FindObjectIndexByIdReturnsNoIndexForUnknownOrUnset)
{
    LevelNs::LevelData level;
    level.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 0));
    LevelNs::EnsureUniqueObjectIds(level);

    EXPECT_EQ(LevelNs::FindObjectIndexById(level, 9999u), LevelNs::kNoObjectIndex);

    // 0 は「object 無し」の番兵なので、未割当 id 0 の実体が混ざっていても引かない
    level.objects.push_back(LevelNs::ObjectInstance{});
    EXPECT_EQ(LevelNs::FindObjectIndexById(level, LevelNs::kNoObjectId), LevelNs::kNoObjectIndex);
}

TEST(ObjectIdTest, AddObjectCommandAssignsIdAndRedoReusesIt)
{
    LevelNs::LevelData level;
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget target{level, ids, next};

    EditorNs::AddObjectCommand cmd(LevelNs::ObjectInstance{});
    cmd.Do(target);
    ASSERT_EQ(level.objects.size(), 1u);
    const std::uint32_t assigned = level.objects[0].objectId;
    EXPECT_NE(assigned, 0u);

    cmd.Undo(target);
    EXPECT_TRUE(level.objects.empty());

    // redo で別 id にならず、消えた間に参照が壊れない
    cmd.Do(target);
    ASSERT_EQ(level.objects.size(), 1u);
    EXPECT_EQ(level.objects[0].objectId, assigned);
}

TEST(ObjectIdTest, DuplicateObjectCommandAssignsFreshId)
{
    LevelNs::LevelData level;
    level.objects.push_back(LevelNs::ObjectInstance{});
    LevelNs::EnsureUniqueObjectIds(level);
    std::vector<std::uint32_t> ids{0};
    std::uint32_t next = 1;
    LevelNs::EditTarget target{level, ids, next};

    EditorNs::DuplicateObjectCommand cmd(0);
    cmd.Do(target);

    ASSERT_EQ(level.objects.size(), 2u);
    EXPECT_TRUE(AllIdsUniqueAndAssigned(level));
    EXPECT_NE(level.objects[1].objectId, level.objects[0].objectId);
}

TEST(ObjectIdTest, PlaceCommandReplaceKeepsPersistentId)
{
    LevelNs::LevelData level;
    level.objects.push_back(LevelNs::MakeGridObject(5, 0, 3, 0));
    LevelNs::EnsureUniqueObjectIds(level);
    const std::uint32_t original = level.objects[0].objectId;
    std::vector<std::uint32_t> ids{0};
    std::uint32_t next = 1;
    LevelNs::EditTarget target{level, ids, next};

    // 同じ cell への配置は置換になり、同じ場所の物として永続 id を引き継ぐ
    EditorNs::PlaceCommand cmd(LevelNs::MakeGridObject(0, 0, 0, 0), 5, 0, 3, 1);
    cmd.Do(target);

    ASSERT_EQ(level.objects.size(), 1u);
    EXPECT_EQ(level.objects[0].objectId, original);
}

TEST(ObjectIdTest, ObjectRefFieldSurvivesJsonRoundTrip)
{
    LevelNs::LevelData level;
    level.objects.push_back(LevelNs::ObjectInstance{});
    level.objects.push_back(LevelNs::ObjectInstance{});
    level.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    // 追従カメラも積んでおく。 読込の門の合成が CRC を動かさないようにするため
    level.objects.push_back(LevelNs::MakeFollowCameraObject(0u));
    LevelNs::EnsureUniqueObjectIds(level);
    const std::uint32_t targetId = level.objects[0].objectId;

    LevelNs::ComponentData comp;
    comp.typeName = "FakeFollowComponent";
    comp.fields.push_back(LevelNs::FieldValue{"Target", NS::Scene::ObjectRef{targetId}});
    level.objects[1].components.push_back(std::move(comp));

    const std::uint32_t crc0 = level.ComputeCrc32();
    const std::string text = LevelNs::SerializeLevelToJson(level);
    LevelNs::LevelData restored;
    ASSERT_TRUE(LevelNs::DeserializeLevelFromJson(restored, text));

    EXPECT_EQ(restored.ComputeCrc32(), crc0);
    ASSERT_EQ(restored.objects.size(), 4u);
    ASSERT_EQ(restored.objects[1].components.size(), 1u);
    const auto* field = LevelNs::FindField(restored.objects[1].components[0], "Target");
    ASSERT_NE(field, nullptr);
    ASSERT_TRUE(std::holds_alternative<NS::Scene::ObjectRef>(field->value));
    EXPECT_EQ(std::get<NS::Scene::ObjectRef>(field->value).id, targetId);
}

TEST(ObjectIdTest, DanglingObjectRefIsPrunedOnLoad)
{
    LevelNs::LevelData level;
    level.objects.push_back(LevelNs::ObjectInstance{});
    level.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    LevelNs::EnsureUniqueObjectIds(level);

    // どの object も持たない id を指す参照を仕込むと、読込で未設定 0 へ戻る
    LevelNs::ComponentData comp;
    comp.typeName = "FakeFollowComponent";
    comp.fields.push_back(LevelNs::FieldValue{"Target", NS::Scene::ObjectRef{9999u}});
    level.objects[0].components.push_back(std::move(comp));

    const std::string text = LevelNs::SerializeLevelToJson(level);
    LevelNs::LevelData restored;
    ASSERT_TRUE(LevelNs::DeserializeLevelFromJson(restored, text));

    // 末尾に追従カメラが 1 台合成される
    ASSERT_EQ(restored.objects.size(), 3u);
    ASSERT_EQ(restored.objects[0].components.size(), 1u);
    const auto* field = LevelNs::FindField(restored.objects[0].components[0], "Target");
    ASSERT_NE(field, nullptr);
    ASSERT_TRUE(std::holds_alternative<NS::Scene::ObjectRef>(field->value));
    EXPECT_FALSE(std::get<NS::Scene::ObjectRef>(field->value).IsSet());
}

TEST(ObjectIdTest, PruneDanglingObjectRefsKeepsValidAndCountsPruned)
{
    LevelNs::LevelData level;
    level.objects.push_back(LevelNs::ObjectInstance{});
    level.objects.push_back(LevelNs::ObjectInstance{});
    LevelNs::EnsureUniqueObjectIds(level);
    const std::uint32_t validId = level.objects[1].objectId;

    LevelNs::ComponentData comp;
    comp.typeName = "FakeFollowComponent";
    comp.fields.push_back(LevelNs::FieldValue{"Valid", NS::Scene::ObjectRef{validId}});
    comp.fields.push_back(LevelNs::FieldValue{"Dangling", NS::Scene::ObjectRef{12345u}});
    comp.fields.push_back(LevelNs::FieldValue{"Unset", NS::Scene::ObjectRef{}});
    level.objects[0].components.push_back(std::move(comp));

    // 有効参照と未設定は数えず、宙参照 1 件だけが直る
    EXPECT_EQ(LevelNs::PruneDanglingObjectRefs(level), 1u);

    const auto& fields = level.objects[0].components[0].fields;
    EXPECT_EQ(std::get<NS::Scene::ObjectRef>(fields[0].value).id, validId);
    EXPECT_FALSE(std::get<NS::Scene::ObjectRef>(fields[1].value).IsSet());
    EXPECT_FALSE(std::get<NS::Scene::ObjectRef>(fields[2].value).IsSet());
}
