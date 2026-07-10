#include "Editor/Undo/AddComponentCommand.h"
#include "Editor/Undo/DuplicateObjectCommand.h"
#include "Editor/Undo/RemoveComponentCommand.h"
#include "Editor/Undo/SetObjectComponentsCommand.h"
#include "Framework/Scene/SceneData.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace EditorNs = NS::Editor;
namespace SceneNs = NS::Scene;

namespace
{
    // components を持たない自由オブジェクトを 1 つ作る
    SceneNs::ObjectData MakeObject()
    {
        SceneNs::ObjectData o{};
        return o;
    }

    // 反射値を 1 つ持つ当たり箱コンポーネント。 Undo 復元で値まで戻ることを確かめる種にする
    SceneNs::ComponentData MakeBoxCollider(float halfExtent)
    {
        SceneNs::ComponentData component;
        component.typeName = "BoxCollider";
        component.fields.push_back(SceneNs::FieldValue{"halfExtent", halfExtent});
        return component;
    }
} // namespace

TEST(ComponentCommand, AddComponentDoAddsOneTypeUndoRemoves)
{
    SceneNs::SceneData lv;
    lv.objects.push_back(MakeObject());
    SceneNs::EnsureUniqueObjectIds(lv);

    const std::uint32_t id = lv.objects[0].objectId;
    EditorNs::AddComponentCommand cmd(id, SceneNs::ComponentData{"HazardComponent"});

    cmd.Do(lv);
    ASSERT_EQ(lv.objects[0].components.size(), 1u);
    EXPECT_EQ(lv.objects[0].components[0].typeName, "HazardComponent");

    cmd.Undo(lv);
    EXPECT_TRUE(lv.objects[0].components.empty());
}

TEST(ComponentCommand, AddComponentAllowsDuplicateType)
{
    SceneNs::SceneData lv;
    lv.objects.push_back(MakeObject());
    lv.objects[0].components.push_back(SceneNs::ComponentData{"HazardComponent"});
    SceneNs::EnsureUniqueObjectIds(lv);

    const std::uint32_t id = lv.objects[0].objectId;
    EditorNs::AddComponentCommand cmd(id, SceneNs::ComponentData{"HazardComponent"});

    cmd.Do(lv); // 同型でも重ねて足せる
    EXPECT_EQ(lv.objects[0].components.size(), 2u);

    cmd.Undo(lv); // 足した 1 つだけ消え、 元からあった同型は残る
    ASSERT_EQ(lv.objects[0].components.size(), 1u);
    EXPECT_EQ(lv.objects[0].components[0].typeName, "HazardComponent");
}

TEST(ComponentCommand, RemoveComponentUndoRestoresFieldValues)
{
    SceneNs::SceneData lv;
    lv.objects.push_back(MakeObject());
    lv.objects[0].components.push_back(SceneNs::ComponentData{"HazardComponent"});
    lv.objects[0].components.push_back(MakeBoxCollider(2.5f));
    SceneNs::EnsureUniqueObjectIds(lv);

    const std::uint32_t id = lv.objects[0].objectId;
    EditorNs::RemoveComponentCommand cmd(id, 1); // BoxCollider は添字 1

    cmd.Do(lv);
    ASSERT_EQ(lv.objects[0].components.size(), 1u);
    EXPECT_EQ(lv.objects[0].components[0].typeName, "HazardComponent");

    cmd.Undo(lv);
    ASSERT_EQ(lv.objects[0].components.size(), 2u);
    // 元の位置と反射値ごと戻ることを確かめる
    const SceneNs::ComponentData& restored = lv.objects[0].components[1];
    EXPECT_EQ(restored.typeName, "BoxCollider");
    ASSERT_EQ(restored.fields.size(), 1u);
    EXPECT_EQ(restored.fields[0].name, "halfExtent");
    ASSERT_TRUE(std::holds_alternative<float>(restored.fields[0].value));
    EXPECT_FLOAT_EQ(std::get<float>(restored.fields[0].value), 2.5f);
}

TEST(ComponentCommand, RemoveComponentRedoRemovesAgain)
{
    SceneNs::SceneData lv;
    lv.objects.push_back(MakeObject());
    lv.objects[0].components.push_back(SceneNs::ComponentData{"HazardComponent"});
    lv.objects[0].components.push_back(MakeBoxCollider(3.0f));
    SceneNs::EnsureUniqueObjectIds(lv);

    const std::uint32_t id = lv.objects[0].objectId;
    EditorNs::RemoveComponentCommand cmd(id, 1); // BoxCollider は添字 1

    cmd.Do(lv);
    cmd.Undo(lv);
    cmd.Do(lv); // 2 回目の Do でも退避がリセットされ同じ型を取り除ける
    ASSERT_EQ(lv.objects[0].components.size(), 1u);
    EXPECT_EQ(lv.objects[0].components[0].typeName, "HazardComponent");

    cmd.Undo(lv); // 2 回目の Undo でも反射値ごと戻る
    ASSERT_EQ(lv.objects[0].components.size(), 2u);
    ASSERT_EQ(lv.objects[0].components[1].fields.size(), 1u);
    EXPECT_FLOAT_EQ(std::get<float>(lv.objects[0].components[1].fields[0].value), 3.0f);
}

TEST(ComponentCommand, DuplicateObjectDeepCopiesComponentsUndoRemoves)
{
    SceneNs::SceneData lv;
    SceneNs::ObjectData src = MakeObject();
    src.positionX = 3.0f;
    src.components.push_back(SceneNs::ComponentData{"HazardComponent"});
    src.components.push_back(MakeBoxCollider(1.5f));
    lv.objects.push_back(src);
    SceneNs::EnsureUniqueObjectIds(lv);

    const std::uint32_t id = lv.objects[0].objectId;
    EditorNs::DuplicateObjectCommand cmd(id);

    cmd.Do(lv);
    ASSERT_EQ(lv.objects.size(), 2u);
    EXPECT_NE(lv.objects[1].objectId, lv.objects[0].objectId); // 複製は別の永続 id を持つ
    const SceneNs::ObjectData& dup = lv.objects[1];
    EXPECT_FLOAT_EQ(dup.positionX, 3.0f);
    ASSERT_EQ(dup.components.size(), 2u);
    EXPECT_EQ(dup.components[0].typeName, "HazardComponent");
    EXPECT_EQ(dup.components[1].typeName, "BoxCollider");
    ASSERT_EQ(dup.components[1].fields.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<float>(dup.components[1].fields[0].value));
    EXPECT_FLOAT_EQ(std::get<float>(dup.components[1].fields[0].value), 1.5f);

    cmd.Undo(lv);
    ASSERT_EQ(lv.objects.size(), 1u);
    EXPECT_EQ(lv.objects[0].objectId, id);
}

TEST(ComponentCommand, DuplicateObjectRedoReusesSameId)
{
    SceneNs::SceneData lv;
    lv.objects.push_back(MakeObject());
    SceneNs::EnsureUniqueObjectIds(lv);

    const std::uint32_t srcId = lv.objects[0].objectId;
    EditorNs::DuplicateObjectCommand cmd(srcId);

    cmd.Do(lv);
    ASSERT_EQ(lv.objects.size(), 2u);
    const std::uint32_t dupId = lv.objects[1].objectId;
    const std::uint32_t counter = lv.nextObjectId;

    cmd.Undo(lv);
    ASSERT_EQ(lv.objects.size(), 1u);

    cmd.Do(lv); // redo: 同じ永続 id を再利用しカウンタは増やさない
    ASSERT_EQ(lv.objects.size(), 2u);
    EXPECT_EQ(lv.objects[1].objectId, dupId);
    EXPECT_EQ(lv.nextObjectId, counter);
}

TEST(ComponentCommand, CommandsOnUnknownIdAreNoOp)
{
    SceneNs::SceneData lv;
    lv.objects.push_back(MakeObject());
    lv.objects[0].components.push_back(MakeBoxCollider(1.0f));
    SceneNs::EnsureUniqueObjectIds(lv);

    const std::uint32_t unknownId = 999u;

    EditorNs::AddComponentCommand add(unknownId, SceneNs::ComponentData{"HazardComponent"});
    add.Do(lv);
    add.Undo(lv);
    EXPECT_EQ(lv.objects[0].components.size(), 1u); // 対象が居ないので増減しない

    EditorNs::RemoveComponentCommand remove(unknownId, 0);
    remove.Do(lv);
    remove.Undo(lv);
    EXPECT_EQ(lv.objects[0].components.size(), 1u);

    EditorNs::DuplicateObjectCommand dup(unknownId);
    dup.Do(lv);
    EXPECT_EQ(lv.objects.size(), 1u);
    dup.Undo(lv);
    EXPECT_EQ(lv.objects.size(), 1u);
}

TEST(ComponentCommand, RemoveLastComponentIsRefusedToAvoidGhost)
{
    SceneNs::SceneData lv;
    lv.objects.push_back(MakeObject());
    lv.objects[0].components.push_back(SceneNs::ComponentData{"MeshRendererComponent"});
    SceneNs::EnsureUniqueObjectIds(lv);

    const std::uint32_t id = lv.objects[0].objectId;
    EditorNs::RemoveComponentCommand cmd(id, 0); // 唯一の component を消そうとする

    cmd.Do(lv); // 空構成は build で不可視ゴーストになるので除去を拒む
    ASSERT_EQ(lv.objects[0].components.size(), 1u);
    EXPECT_EQ(lv.objects[0].components[0].typeName, "MeshRendererComponent");

    cmd.Undo(lv); // 退避が無いので Undo も何もしない
    ASSERT_EQ(lv.objects[0].components.size(), 1u);
    EXPECT_EQ(lv.objects[0].components[0].typeName, "MeshRendererComponent");
}

TEST(ComponentCommand, SetObjectComponentsReplacesWholeListUndoRestores)
{
    SceneNs::SceneData lv;
    lv.objects.push_back(MakeObject());
    lv.objects[0].components.push_back(SceneNs::ComponentData{"MeshRendererComponent"});
    SceneNs::EnsureUniqueObjectIds(lv);

    const std::uint32_t id = lv.objects[0].objectId;
    std::vector<SceneNs::ComponentData> replacement;
    replacement.push_back(SceneNs::ComponentData{"MeshRendererComponent"});
    replacement.push_back(MakeBoxCollider(0.5f));
    replacement.push_back(SceneNs::ComponentData{"HazardComponent"});
    EditorNs::SetObjectComponentsCommand cmd(id, replacement);

    cmd.Do(lv);
    ASSERT_EQ(lv.objects[0].components.size(), 3u);
    EXPECT_EQ(lv.objects[0].components[1].typeName, "BoxCollider");
    EXPECT_EQ(lv.objects[0].components[2].typeName, "HazardComponent");

    cmd.Undo(lv); // 置換前の 1 件だけの一覧へ戻る
    ASSERT_EQ(lv.objects[0].components.size(), 1u);
    EXPECT_EQ(lv.objects[0].components[0].typeName, "MeshRendererComponent");

    cmd.Do(lv); // redo: 退避した旧一覧を上書きしても同じ置換結果になる
    ASSERT_EQ(lv.objects[0].components.size(), 3u);
    EXPECT_EQ(lv.objects[0].components[2].typeName, "HazardComponent");

    cmd.Undo(lv); // 再び置換前の 1 件へ戻る
    ASSERT_EQ(lv.objects[0].components.size(), 1u);
    EXPECT_EQ(lv.objects[0].components[0].typeName, "MeshRendererComponent");
}
