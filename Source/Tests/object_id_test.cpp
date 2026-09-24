#include "Editor/EditorObjects.h"
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

namespace SceneNs = NS::Obj;

namespace
{
    [[nodiscard]] bool AllIdsUniqueAndAssigned(const nlohmann::json& level)
    {
        std::unordered_set<std::uint32_t> seen;
        for (const nlohmann::json& object : SceneNs::SceneJsonObjects(level))
        {
            const std::uint32_t objectId = SceneNs::ObjectJsonId(object);
            if (objectId == 0 || !seen.insert(objectId).second)
                return false;
        }
        return true;
    }
} // namespace

TEST(ObjectIdTest, EnsureUniqueAssignsMissingIds)
{
    nlohmann::json level = SceneNs::MakeSceneJson();
    nlohmann::json& objects = SceneNs::SceneJsonObjects(level);
    objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    objects.push_back(NS::Editor::MakeCellObject(1, 0, 0));
    objects.push_back(NS::Editor::MakeCellObject(2, 0, 0));

    SceneNs::EnsureUniqueObjectIds(level);

    EXPECT_TRUE(AllIdsUniqueAndAssigned(level));
    // カウンタは既存最大 id の先を指す
    for (const nlohmann::json& object : objects)
        EXPECT_LT(SceneNs::ObjectJsonId(object), SceneNs::SceneJsonNextObjectId(level));
}

TEST(ObjectIdTest, EnsureUniqueReassignsDuplicatesKeepingFirst)
{
    nlohmann::json level = SceneNs::MakeSceneJson();
    nlohmann::json& objects = SceneNs::SceneJsonObjects(level);
    objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    objects.push_back(NS::Editor::MakeCellObject(1, 0, 0));
    objects.push_back(NS::Editor::MakeCellObject(2, 0, 0));
    SceneNs::SetObjectJsonId(objects[0], 5u);
    SceneNs::SetObjectJsonId(objects[1], 5u);
    SceneNs::SetObjectJsonId(objects[2], 2u);

    SceneNs::EnsureUniqueObjectIds(level);

    // 先勝ちで最初の 5 は保持、2 番目に新 id、既存の 2 も保持
    EXPECT_EQ(SceneNs::ObjectJsonId(objects[0]), 5u);
    EXPECT_NE(SceneNs::ObjectJsonId(objects[1]), 5u);
    EXPECT_EQ(SceneNs::ObjectJsonId(objects[2]), 2u);
    EXPECT_TRUE(AllIdsUniqueAndAssigned(level));
}

TEST(ObjectIdTest, JsonRoundTripPreservesIdsAndCounter)
{
    nlohmann::json level = SceneNs::MakeSceneJson();
    nlohmann::json& objects = SceneNs::SceneJsonObjects(level);
    objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    objects.push_back(NS::Editor::MakeCellObject(3, 1, 2));
    objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    SceneNs::EnsureUniqueObjectIds(level);
    const std::uint32_t id0 = SceneNs::ObjectJsonId(objects[0]);
    const std::uint32_t id1 = SceneNs::ObjectJsonId(objects[1]);
    const std::uint32_t counter = SceneNs::SceneJsonNextObjectId(level);

    const std::string text = SceneNs::SerializeSceneToJson(level);
    nlohmann::json restored = SceneNs::MakeSceneJson();
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(restored, text));

    const nlohmann::json& restoredObjects = SceneNs::SceneJsonObjects(restored);
    ASSERT_EQ(restoredObjects.size(), 4u);
    EXPECT_EQ(SceneNs::ObjectJsonId(restoredObjects[0]), id0);
    EXPECT_EQ(SceneNs::ObjectJsonId(restoredObjects[1]), id1);
    EXPECT_EQ(SceneNs::SceneJsonNextObjectId(restored), counter);
}

TEST(ObjectIdTest, JsonWithoutIdsGetsAssignedOnLoad)
{
    // id と nextObjectId を欠いた手編集ファイルを読むと採番される
    const std::string handEdited = R"({
        "version": 4,
        "objects": [
            {"position": [0.0, 0.0, 0.0], "components": []},
            {"position": [1.0, 0.0, 0.0], "components": []}
        ]
    })";

    nlohmann::json restored = SceneNs::MakeSceneJson();
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(restored, handEdited));
    ASSERT_EQ(SceneNs::SceneJsonObjects(restored).size(), 2u);
    EXPECT_TRUE(AllIdsUniqueAndAssigned(restored));
}

TEST(ObjectIdTest, FindObjectIndexByIdReturnsMatchingIndex)
{
    nlohmann::json level = SceneNs::MakeSceneJson();
    nlohmann::json& objects = SceneNs::SceneJsonObjects(level);
    objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    objects.push_back(NS::Editor::MakeCellObject(1, 0, 0));
    objects.push_back(NS::Editor::MakeCellObject(2, 0, 0));
    SceneNs::EnsureUniqueObjectIds(level);

    const std::uint32_t id = SceneNs::ObjectJsonId(objects[1]);
    EXPECT_EQ(SceneNs::FindObjectIndexById(level, id), 1u);
}

TEST(ObjectIdTest, FindObjectIndexByIdReturnsNoIndexForUnknownOrUnset)
{
    nlohmann::json level = SceneNs::MakeSceneJson();
    nlohmann::json& objects = SceneNs::SceneJsonObjects(level);
    objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    SceneNs::EnsureUniqueObjectIds(level);

    EXPECT_EQ(SceneNs::FindObjectIndexById(level, 9999u), SceneNs::k_NoObjectIndex);

    // 0 は「object 無し」の番兵なので、未割当 id 0 の実体が混ざっていても引かない
    objects.push_back(SceneNs::MakeObjectJson());
    EXPECT_EQ(SceneNs::FindObjectIndexById(level, SceneNs::k_NoObjectId), SceneNs::k_NoObjectIndex);
}

TEST(ObjectIdTest, ObjectRefFieldSurvivesJsonRoundTrip)
{
    nlohmann::json level = SceneNs::MakeSceneJson();
    nlohmann::json& objects = SceneNs::SceneJsonObjects(level);
    objects.push_back(SceneNs::MakeObjectJson());
    objects.push_back(SceneNs::MakeObjectJson());
    objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    SceneNs::EnsureUniqueObjectIds(level);
    const std::uint32_t targetId = SceneNs::ObjectJsonId(objects[0]);

    nlohmann::json comp = SceneNs::MakeComponentEntry("FakeFollowComponent");
    SceneNs::SetField(comp, "Target", NS::Obj::ObjectRef{targetId});
    SceneNs::ObjectJsonComponents(objects[1]).push_back(std::move(comp));

    // 読込が全 object に transform を保証するため、比べる元も transform 込みで揃える
    SceneNs::EnsureTransformComponent(objects[0]);
    SceneNs::EnsureTransformComponent(objects[1]);
    // 後から足した component は未採番。読込時の採番が番号を振るので、比べる元も採番済から取る
    SceneNs::EnsureUniqueObjectIds(level);

    const std::string text = SceneNs::SerializeSceneToJson(level);
    nlohmann::json restored = SceneNs::MakeSceneJson();
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(restored, text));

    EXPECT_TRUE(restored == level);
    const nlohmann::json& restoredObjects = SceneNs::SceneJsonObjects(restored);
    ASSERT_EQ(restoredObjects.size(), 4u);
    ASSERT_EQ(SceneNs::ObjectJsonComponents(restoredObjects[1]).size(), 2u); // FakeFollow + transform
    const nlohmann::json& entry = SceneNs::ObjectJsonComponents(restoredObjects[1])[0];
    ASSERT_TRUE(SceneNs::HasField(entry, "Target"));
    EXPECT_EQ(SceneNs::FieldObjectRef(entry, "Target").id, targetId);
}

TEST(ObjectIdTest, DanglingObjectRefIsPrunedOnLoad)
{
    nlohmann::json level = SceneNs::MakeSceneJson();
    nlohmann::json& objects = SceneNs::SceneJsonObjects(level);
    objects.push_back(SceneNs::MakeObjectJson());
    objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    SceneNs::EnsureUniqueObjectIds(level);

    // どの object も持たない id を指す参照を仕込むと、読込で未設定 0 へ戻る
    nlohmann::json comp = SceneNs::MakeComponentEntry("FakeFollowComponent");
    SceneNs::SetField(comp, "Target", NS::Obj::ObjectRef{9999u});
    SceneNs::ObjectJsonComponents(objects[0]).push_back(std::move(comp));

    const std::string text = SceneNs::SerializeSceneToJson(level);
    nlohmann::json restored = SceneNs::MakeSceneJson();
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(restored, text));

    const nlohmann::json& restoredObjects = SceneNs::SceneJsonObjects(restored);
    ASSERT_EQ(restoredObjects.size(), 2u);
    ASSERT_EQ(SceneNs::ObjectJsonComponents(restoredObjects[0]).size(), 2u); // FakeFollow + transform
    const nlohmann::json& entry = SceneNs::ObjectJsonComponents(restoredObjects[0])[0];
    ASSERT_TRUE(SceneNs::HasField(entry, "Target"));
    EXPECT_FALSE(SceneNs::FieldObjectRef(entry, "Target").IsSet());
}

TEST(ObjectIdTest, PruneDanglingObjectRefsKeepsValidAndCountsPruned)
{
    nlohmann::json level = SceneNs::MakeSceneJson();
    nlohmann::json& objects = SceneNs::SceneJsonObjects(level);
    objects.push_back(SceneNs::MakeObjectJson());
    objects.push_back(SceneNs::MakeObjectJson());
    SceneNs::EnsureUniqueObjectIds(level);
    const std::uint32_t validId = SceneNs::ObjectJsonId(objects[1]);

    nlohmann::json comp = SceneNs::MakeComponentEntry("FakeFollowComponent");
    SceneNs::SetField(comp, "Valid", NS::Obj::ObjectRef{validId});
    SceneNs::SetField(comp, "Dangling", NS::Obj::ObjectRef{12345u});
    SceneNs::SetField(comp, "Unset", NS::Obj::ObjectRef{});
    SceneNs::ObjectJsonComponents(objects[0]).push_back(std::move(comp));

    // 有効参照と未設定は数えず、宙参照 1 件だけが直る
    EXPECT_EQ(SceneNs::PruneDanglingObjectRefs(level), 1u);

    const nlohmann::json& entry = SceneNs::ObjectJsonComponents(objects[0])[0];
    EXPECT_EQ(SceneNs::FieldObjectRef(entry, "Valid").id, validId);
    EXPECT_FALSE(SceneNs::FieldObjectRef(entry, "Dangling").IsSet());
    EXPECT_FALSE(SceneNs::FieldObjectRef(entry, "Unset").IsSet());
}

TEST(ObjectIdTest, PruneInvalidParentsKeepsValidChain)
{
    nlohmann::json level = SceneNs::MakeSceneJson();
    nlohmann::json& objects = SceneNs::SceneJsonObjects(level);
    objects.push_back(SceneNs::MakeObjectJson());
    objects.push_back(SceneNs::MakeObjectJson());
    objects.push_back(SceneNs::MakeObjectJson());
    SceneNs::EnsureUniqueObjectIds(level);
    SceneNs::SetObjectJsonParent(objects[1], SceneNs::ObjectJsonId(objects[0]));
    SceneNs::SetObjectJsonParent(objects[2], SceneNs::ObjectJsonId(objects[1]));

    EXPECT_EQ(SceneNs::PruneInvalidParents(level), 0u);
    EXPECT_EQ(SceneNs::ObjectJsonParent(objects[1]), SceneNs::ObjectJsonId(objects[0]));
    EXPECT_EQ(SceneNs::ObjectJsonParent(objects[2]), SceneNs::ObjectJsonId(objects[1]));
}

TEST(ObjectIdTest, PruneInvalidParentsBreaksCyclesAndDanglingParents)
{
    nlohmann::json level = SceneNs::MakeSceneJson();
    nlohmann::json& objects = SceneNs::SceneJsonObjects(level);
    objects.push_back(SceneNs::MakeObjectJson());
    objects.push_back(SceneNs::MakeObjectJson());
    objects.push_back(SceneNs::MakeObjectJson());
    SceneNs::EnsureUniqueObjectIds(level);

    // 互いを親にした輪。組んだままだと world 変換の再帰が止まらない
    SceneNs::SetObjectJsonParent(objects[0], SceneNs::ObjectJsonId(objects[1]));
    SceneNs::SetObjectJsonParent(objects[1], SceneNs::ObjectJsonId(objects[0]));
    SceneNs::SetObjectJsonParent(objects[2], 12345u);

    // 輪は 1 本切れば解ける。残りは正当な親子として通るので、直る件数は輪 1 + 宙 1
    EXPECT_EQ(SceneNs::PruneInvalidParents(level), 2u);
    EXPECT_EQ(SceneNs::ObjectJsonParent(objects[2]), SceneNs::k_NoObjectId);

    // 輪が残っていなければ 2 度目は何も直さない
    EXPECT_EQ(SceneNs::PruneInvalidParents(level), 0u);
}

TEST(ObjectIdTest, PruneInvalidParentsRejectsSelfParent)
{
    nlohmann::json level = SceneNs::MakeSceneJson();
    nlohmann::json& objects = SceneNs::SceneJsonObjects(level);
    objects.push_back(SceneNs::MakeObjectJson());
    SceneNs::EnsureUniqueObjectIds(level);
    SceneNs::SetObjectJsonParent(objects[0], SceneNs::ObjectJsonId(objects[0]));

    EXPECT_EQ(SceneNs::PruneInvalidParents(level), 1u);
    EXPECT_EQ(SceneNs::ObjectJsonParent(objects[0]), SceneNs::k_NoObjectId);
}

// 付いている名前は保ち、空と重複にだけ UE と同じく番号付きの名前を振る
TEST(ObjectNameTest, EnsureUniqueObjectNamesKeepsGivenNamesAndNumbersTheRest)
{
    nlohmann::json level = SceneNs::MakeSceneJson();
    nlohmann::json& objects = SceneNs::SceneJsonObjects(level);
    for (int i = 0; i < 4; ++i)
        objects.push_back(SceneNs::MakeObjectJson());
    SceneNs::SetObjectJsonName(objects[1], "Object");
    SceneNs::SetObjectJsonName(objects[3], "Object");

    SceneNs::EnsureUniqueObjectNames(level);

    EXPECT_EQ(SceneNs::ObjectJsonName(objects[0]), "Object_1");
    EXPECT_EQ(SceneNs::ObjectJsonName(objects[1]), "Object");
    EXPECT_EQ(SceneNs::ObjectJsonName(objects[2]), "Object_2");
    EXPECT_EQ(SceneNs::ObjectJsonName(objects[3]), "Object_3");
}

// component の名前は配置物の中で一意。付いている名前を保ち、無い物は型名、重なった物は番号付きにする
TEST(ObjectNameTest, EnsureUniqueObjectNamesNamesComponentsWithinTheirObject)
{
    nlohmann::json level = SceneNs::MakeSceneJson();
    nlohmann::json& objects = SceneNs::SceneJsonObjects(level);
    objects.push_back(SceneNs::MakeObjectJson());
    objects.push_back(SceneNs::MakeObjectJson());
    nlohmann::json named = SceneNs::MakeComponentEntry("BoxCollider");
    SceneNs::SetComponentEntryName(named, "Gate");
    nlohmann::json& firstComponents = SceneNs::ObjectJsonComponents(objects[0]);
    firstComponents.push_back(named);
    firstComponents.push_back(SceneNs::MakeComponentEntry("BoxCollider"));
    firstComponents.push_back(SceneNs::MakeComponentEntry("BoxCollider"));
    firstComponents.push_back(named);
    // 別の配置物の名前とは重なってよい
    SceneNs::ObjectJsonComponents(objects[1]).push_back(SceneNs::MakeComponentEntry("BoxCollider"));

    SceneNs::EnsureUniqueObjectNames(level);

    const nlohmann::json& first = SceneNs::ObjectJsonComponents(objects[0]);
    EXPECT_EQ(SceneNs::ComponentEntryName(first[0]), "Gate");
    EXPECT_EQ(SceneNs::ComponentEntryName(first[1]), "BoxCollider");
    EXPECT_EQ(SceneNs::ComponentEntryName(first[2]), "BoxCollider_1");
    EXPECT_EQ(SceneNs::ComponentEntryName(first[3]), "Gate_1");
    EXPECT_EQ(SceneNs::ComponentEntryName(SceneNs::ObjectJsonComponents(objects[1])[0]), "BoxCollider");
}
