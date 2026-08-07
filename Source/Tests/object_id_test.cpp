#include "Game/Level/BlockObject.h"
#include "Game/Level/FollowCameraObject.h"
#include "Game/Player.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/ObjectRef.h"
#include "Runtime/Object/Scene/SceneJson.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <unordered_set>
#include <vector>

namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Object;

namespace
{
    [[nodiscard]] bool AllIdsUniqueAndAssigned(const SceneNs::SceneData& level)
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
    SceneNs::SceneData level;
    level.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    level.objects.push_back(LevelNs::MakeCellObject(1, 0, 0));
    level.objects.push_back(LevelNs::MakeCellObject(2, 0, 0));

    SceneNs::EnsureUniqueObjectIds(level);

    EXPECT_TRUE(AllIdsUniqueAndAssigned(level));
    // カウンタは既存最大 id の先を指す
    for (const auto& object : level.objects)
        EXPECT_LT(object.objectId, level.nextObjectId);
}

TEST(ObjectIdTest, EnsureUniqueReassignsDuplicatesKeepingFirst)
{
    SceneNs::SceneData level;
    level.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    level.objects.push_back(LevelNs::MakeCellObject(1, 0, 0));
    level.objects.push_back(LevelNs::MakeCellObject(2, 0, 0));
    level.objects[0].objectId = 5;
    level.objects[1].objectId = 5;
    level.objects[2].objectId = 2;

    SceneNs::EnsureUniqueObjectIds(level);

    // 先勝ちで最初の 5 は保持、2 番目に新 id、既存の 2 も保持
    EXPECT_EQ(level.objects[0].objectId, 5u);
    EXPECT_NE(level.objects[1].objectId, 5u);
    EXPECT_EQ(level.objects[2].objectId, 2u);
    EXPECT_TRUE(AllIdsUniqueAndAssigned(level));
}

TEST(ObjectIdTest, JsonRoundTripPreservesIdsAndCounter)
{
    SceneNs::SceneData level;
    level.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    level.objects.push_back(LevelNs::MakeCellObject(3, 1, 2));
    level.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    // 追従カメラも積んでおく。 読込時の既定合成で採番カウンタが動くと counter 比較が成り立たないため
    level.objects.push_back(NS::Game::Level::MakeFollowCameraObject(0u));
    SceneNs::EnsureUniqueObjectIds(level);
    const std::uint32_t id0 = level.objects[0].objectId;
    const std::uint32_t id1 = level.objects[1].objectId;
    const std::uint32_t counter = level.nextObjectId;

    const std::string text = SceneNs::SerializeSceneToJson(level);
    SceneNs::SceneData restored;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(restored, text));

    ASSERT_EQ(restored.objects.size(), 4u);
    EXPECT_EQ(restored.objects[0].objectId, id0);
    EXPECT_EQ(restored.objects[1].objectId, id1);
    EXPECT_EQ(restored.nextObjectId, counter);
}

TEST(ObjectIdTest, JsonWithoutIdsGetsAssignedOnLoad)
{
    // id と nextObjectId を欠いた手編集ファイルを読むと採番される
    const std::string handEdited = R"({
        "version": 3,
        "objects": [
            {"position": [0.0, 0.0, 0.0], "components": []},
            {"position": [1.0, 0.0, 0.0], "components": []}
        ]
    })";

    SceneNs::SceneData restored;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(restored, handEdited));
    ASSERT_EQ(restored.objects.size(), 2u);
    EXPECT_TRUE(AllIdsUniqueAndAssigned(restored));
}

TEST(ObjectIdTest, FindObjectIndexByIdReturnsMatchingIndex)
{
    SceneNs::SceneData level;
    level.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    level.objects.push_back(LevelNs::MakeCellObject(1, 0, 0));
    level.objects.push_back(LevelNs::MakeCellObject(2, 0, 0));
    SceneNs::EnsureUniqueObjectIds(level);

    const std::uint32_t id = level.objects[1].objectId;
    EXPECT_EQ(SceneNs::FindObjectIndexById(level, id), 1u);
}

TEST(ObjectIdTest, FindObjectIndexByIdReturnsNoIndexForUnknownOrUnset)
{
    SceneNs::SceneData level;
    level.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    SceneNs::EnsureUniqueObjectIds(level);

    EXPECT_EQ(SceneNs::FindObjectIndexById(level, 9999u), SceneNs::k_NoObjectIndex);

    // 0 は「object 無し」の番兵なので、未割当 id 0 の実体が混ざっていても引かない
    level.objects.push_back(SceneNs::ObjectData{});
    EXPECT_EQ(SceneNs::FindObjectIndexById(level, SceneNs::k_NoObjectId), SceneNs::k_NoObjectIndex);
}

TEST(ObjectIdTest, ObjectRefFieldSurvivesJsonRoundTrip)
{
    SceneNs::SceneData level;
    level.objects.push_back(SceneNs::ObjectData{});
    level.objects.push_back(SceneNs::ObjectData{});
    level.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    // 追従カメラも積んでおく。 読込時の既定合成が CRC を動かさないようにするため
    level.objects.push_back(NS::Game::Level::MakeFollowCameraObject(0u));
    SceneNs::EnsureUniqueObjectIds(level);
    const std::uint32_t targetId = level.objects[0].objectId;

    nlohmann::json comp = SceneNs::MakeComponentEntry("FakeFollowComponent");
    SceneNs::SetField(comp, "Target", NS::Object::ObjectRef{targetId});
    level.objects[1].components.push_back(std::move(comp));

    // 読込が全 object に transform を保証するため、 基準 CRC も transform 込みで取る
    SceneNs::EnsureTransformComponent(level.objects[0]);
    SceneNs::EnsureTransformComponent(level.objects[1]);
    // 後から足した component は未採番。 読込時の採番が番号を振るので、 基準 CRC も採番済から取る
    SceneNs::EnsureUniqueObjectIds(level);

    const std::uint32_t crc0 = level.ComputeCrc32();
    const std::string text = SceneNs::SerializeSceneToJson(level);
    SceneNs::SceneData restored;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(restored, text));

    EXPECT_EQ(restored.ComputeCrc32(), crc0);
    ASSERT_EQ(restored.objects.size(), 4u);
    ASSERT_EQ(restored.objects[1].components.size(), 2u); // FakeFollow + transform
    const nlohmann::json& entry = restored.objects[1].components[0];
    ASSERT_TRUE(SceneNs::HasField(entry, "Target"));
    EXPECT_EQ(SceneNs::FieldObjectRef(entry, "Target").id, targetId);
}

TEST(ObjectIdTest, DanglingObjectRefIsPrunedOnLoad)
{
    SceneNs::SceneData level;
    level.objects.push_back(SceneNs::ObjectData{});
    level.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    SceneNs::EnsureUniqueObjectIds(level);

    // どの object も持たない id を指す参照を仕込むと、読込で未設定 0 へ戻る
    nlohmann::json comp = SceneNs::MakeComponentEntry("FakeFollowComponent");
    SceneNs::SetField(comp, "Target", NS::Object::ObjectRef{9999u});
    level.objects[0].components.push_back(std::move(comp));

    const std::string text = SceneNs::SerializeSceneToJson(level);
    SceneNs::SceneData restored;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(restored, text));

    ASSERT_EQ(restored.objects.size(), 2u);
    ASSERT_EQ(restored.objects[0].components.size(), 2u); // FakeFollow + transform
    const nlohmann::json& entry = restored.objects[0].components[0];
    ASSERT_TRUE(SceneNs::HasField(entry, "Target"));
    EXPECT_FALSE(SceneNs::FieldObjectRef(entry, "Target").IsSet());
}

TEST(ObjectIdTest, PruneDanglingObjectRefsKeepsValidAndCountsPruned)
{
    SceneNs::SceneData level;
    level.objects.push_back(SceneNs::ObjectData{});
    level.objects.push_back(SceneNs::ObjectData{});
    SceneNs::EnsureUniqueObjectIds(level);
    const std::uint32_t validId = level.objects[1].objectId;

    nlohmann::json comp = SceneNs::MakeComponentEntry("FakeFollowComponent");
    SceneNs::SetField(comp, "Valid", NS::Object::ObjectRef{validId});
    SceneNs::SetField(comp, "Dangling", NS::Object::ObjectRef{12345u});
    SceneNs::SetField(comp, "Unset", NS::Object::ObjectRef{});
    level.objects[0].components.push_back(std::move(comp));

    // 有効参照と未設定は数えず、宙参照 1 件だけが直る
    EXPECT_EQ(SceneNs::PruneDanglingObjectRefs(level), 1u);

    const nlohmann::json& entry = level.objects[0].components[0];
    EXPECT_EQ(SceneNs::FieldObjectRef(entry, "Valid").id, validId);
    EXPECT_FALSE(SceneNs::FieldObjectRef(entry, "Dangling").IsSet());
    EXPECT_FALSE(SceneNs::FieldObjectRef(entry, "Unset").IsSet());
}

TEST(ObjectIdTest, PruneInvalidParentsKeepsValidChain)
{
    SceneNs::SceneData level;
    level.objects.push_back(SceneNs::ObjectData{});
    level.objects.push_back(SceneNs::ObjectData{});
    level.objects.push_back(SceneNs::ObjectData{});
    SceneNs::EnsureUniqueObjectIds(level);
    level.objects[1].parentId = level.objects[0].objectId;
    level.objects[2].parentId = level.objects[1].objectId;

    EXPECT_EQ(SceneNs::PruneInvalidParents(level), 0u);
    EXPECT_EQ(level.objects[1].parentId, level.objects[0].objectId);
    EXPECT_EQ(level.objects[2].parentId, level.objects[1].objectId);
}

TEST(ObjectIdTest, PruneInvalidParentsBreaksCyclesAndDanglingParents)
{
    SceneNs::SceneData level;
    level.objects.push_back(SceneNs::ObjectData{});
    level.objects.push_back(SceneNs::ObjectData{});
    level.objects.push_back(SceneNs::ObjectData{});
    SceneNs::EnsureUniqueObjectIds(level);

    // 互いを親にした輪。組んだままだと world 変換の再帰が止まらない
    level.objects[0].parentId = level.objects[1].objectId;
    level.objects[1].parentId = level.objects[0].objectId;
    level.objects[2].parentId = 12345u;

    // 輪は 1 本切れば解ける。残りは正当な親子として通るので、直る件数は輪 1 + 宙 1
    EXPECT_EQ(SceneNs::PruneInvalidParents(level), 2u);
    EXPECT_EQ(level.objects[2].parentId, SceneNs::k_NoObjectId);

    // 輪が残っていなければ 2 度目は何も直さない
    EXPECT_EQ(SceneNs::PruneInvalidParents(level), 0u);
}

TEST(ObjectIdTest, PruneInvalidParentsRejectsSelfParent)
{
    SceneNs::SceneData level;
    level.objects.push_back(SceneNs::ObjectData{});
    SceneNs::EnsureUniqueObjectIds(level);
    level.objects[0].parentId = level.objects[0].objectId;

    EXPECT_EQ(SceneNs::PruneInvalidParents(level), 1u);
    EXPECT_EQ(level.objects[0].parentId, SceneNs::k_NoObjectId);
}

TEST(ObjectIdTest, FindReferencesToCollectsPointingFieldsOnly)
{
    SceneNs::SceneData level;
    level.objects.push_back(SceneNs::ObjectData{}); // [0] 参照先
    level.objects.push_back(SceneNs::ObjectData{}); // [1] target を指す
    level.objects.push_back(SceneNs::ObjectData{}); // [2] 別 id を指す
    SceneNs::EnsureUniqueObjectIds(level);
    const std::uint32_t targetId = level.objects[0].objectId;
    const std::uint32_t otherId = level.objects[2].objectId;

    nlohmann::json a = SceneNs::MakeComponentEntry("FakeFollowComponent");
    SceneNs::SetField(a, "Idle", 1.0f); // ObjectRef でない欄は無視される
    SceneNs::SetField(a, "Target", NS::Object::ObjectRef{targetId});
    level.objects[1].components.push_back(std::move(a));

    nlohmann::json b = SceneNs::MakeComponentEntry("FakeFollowComponent");
    SceneNs::SetField(b, "Target", NS::Object::ObjectRef{otherId});
    level.objects[2].components.push_back(std::move(b));

    const auto refs = SceneNs::FindReferencesTo(level, targetId);
    ASSERT_EQ(refs.size(), 1u);
    EXPECT_EQ(refs[0].objectId, level.objects[1].objectId);
    EXPECT_EQ(refs[0].componentIndex, 0u);
    EXPECT_EQ(refs[0].fieldName, "Target");
}

TEST(ObjectIdTest, FindReferencesToIsEmptyForNoReferrersOrUnsetTarget)
{
    SceneNs::SceneData level;
    level.objects.push_back(SceneNs::ObjectData{});
    level.objects.push_back(SceneNs::ObjectData{});
    SceneNs::EnsureUniqueObjectIds(level);

    // 誰も指していない object は空
    EXPECT_TRUE(SceneNs::FindReferencesTo(level, level.objects[0].objectId).empty());
    // 未設定 id (0) を指す参照は「参照」ではない
    EXPECT_TRUE(SceneNs::FindReferencesTo(level, SceneNs::k_NoObjectId).empty());
}
