#include "Editor/Undo/AddComponentCommand.h"
#include "Editor/Undo/DuplicateObjectCommand.h"
#include "Editor/Undo/RemoveComponentCommand.h"
#include "Editor/Undo/SetObjectComponentsCommand.h"
#include "Game/Level/EditTarget.h"
#include "Game/Level/LevelData.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace EditorNs = NS::Editor;
namespace LevelNs = NS::Game::Level;

namespace
{
    // components を持たない自由オブジェクトを 1 つ作る
    LevelNs::ObjectInstance MakeObject()
    {
        LevelNs::ObjectInstance o{};
        return o;
    }

    // 反射値を 1 つ持つ当たり箱コンポーネント。 Undo 復元で値まで戻ることを確かめる種にする
    LevelNs::ComponentData MakeBoxCollider(float halfExtent)
    {
        LevelNs::ComponentData component;
        component.typeName = "BoxCollider";
        component.fields.push_back(LevelNs::FieldValue{"halfExtent", halfExtent});
        return component;
    }
} // namespace

TEST(ComponentCommand, AddComponentDoAddsOneTypeUndoRemoves)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(MakeObject());
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};
    LevelNs::ResetEditIds(t);

    const std::uint32_t id = LevelNs::IdAt(t, 0);
    EditorNs::AddComponentCommand cmd(id, LevelNs::ComponentData{"PoleComponent"});

    cmd.Do(t);
    ASSERT_EQ(lv.objects[0].components.size(), 1u);
    EXPECT_EQ(lv.objects[0].components[0].typeName, "PoleComponent");

    cmd.Undo(t);
    EXPECT_TRUE(lv.objects[0].components.empty());
}

TEST(ComponentCommand, AddComponentAllowsDuplicateType)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(MakeObject());
    lv.objects[0].components.push_back(LevelNs::ComponentData{"PoleComponent"});
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};
    LevelNs::ResetEditIds(t);

    const std::uint32_t id = LevelNs::IdAt(t, 0);
    EditorNs::AddComponentCommand cmd(id, LevelNs::ComponentData{"PoleComponent"});

    cmd.Do(t); // 同型でも重ねて足せる
    EXPECT_EQ(lv.objects[0].components.size(), 2u);

    cmd.Undo(t); // 足した 1 つだけ消え、 元からあった同型は残る
    ASSERT_EQ(lv.objects[0].components.size(), 1u);
    EXPECT_EQ(lv.objects[0].components[0].typeName, "PoleComponent");
}

TEST(ComponentCommand, RemoveComponentUndoRestoresFieldValues)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(MakeObject());
    lv.objects[0].components.push_back(LevelNs::ComponentData{"PoleComponent"});
    lv.objects[0].components.push_back(MakeBoxCollider(2.5f));
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};
    LevelNs::ResetEditIds(t);

    const std::uint32_t id = LevelNs::IdAt(t, 0);
    EditorNs::RemoveComponentCommand cmd(id, 1); // BoxCollider は添字 1

    cmd.Do(t);
    ASSERT_EQ(lv.objects[0].components.size(), 1u);
    EXPECT_EQ(lv.objects[0].components[0].typeName, "PoleComponent");

    cmd.Undo(t);
    ASSERT_EQ(lv.objects[0].components.size(), 2u);
    // 元の位置と反射値ごと戻ることを確かめる
    const LevelNs::ComponentData& restored = lv.objects[0].components[1];
    EXPECT_EQ(restored.typeName, "BoxCollider");
    ASSERT_EQ(restored.fields.size(), 1u);
    EXPECT_EQ(restored.fields[0].name, "halfExtent");
    ASSERT_TRUE(std::holds_alternative<float>(restored.fields[0].value));
    EXPECT_FLOAT_EQ(std::get<float>(restored.fields[0].value), 2.5f);
}

TEST(ComponentCommand, RemoveComponentRedoRemovesAgain)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(MakeObject());
    lv.objects[0].components.push_back(LevelNs::ComponentData{"PoleComponent"});
    lv.objects[0].components.push_back(MakeBoxCollider(3.0f));
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};
    LevelNs::ResetEditIds(t);

    const std::uint32_t id = LevelNs::IdAt(t, 0);
    EditorNs::RemoveComponentCommand cmd(id, 1); // BoxCollider は添字 1

    cmd.Do(t);
    cmd.Undo(t);
    cmd.Do(t); // 2 回目の Do でも退避がリセットされ同じ型を取り除ける
    ASSERT_EQ(lv.objects[0].components.size(), 1u);
    EXPECT_EQ(lv.objects[0].components[0].typeName, "PoleComponent");

    cmd.Undo(t); // 2 回目の Undo でも反射値ごと戻る
    ASSERT_EQ(lv.objects[0].components.size(), 2u);
    ASSERT_EQ(lv.objects[0].components[1].fields.size(), 1u);
    EXPECT_FLOAT_EQ(std::get<float>(lv.objects[0].components[1].fields[0].value), 3.0f);
}

TEST(ComponentCommand, DuplicateObjectDeepCopiesComponentsUndoRemoves)
{
    LevelNs::LevelData lv;
    LevelNs::ObjectInstance src = MakeObject();
    src.positionX = 3.0f;
    src.components.push_back(LevelNs::ComponentData{"PoleComponent"});
    src.components.push_back(MakeBoxCollider(1.5f));
    lv.objects.push_back(src);
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};
    LevelNs::ResetEditIds(t);

    const std::uint32_t id = LevelNs::IdAt(t, 0);
    EditorNs::DuplicateObjectCommand cmd(id);

    cmd.Do(t);
    ASSERT_EQ(lv.objects.size(), 2u);
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_NE(ids[1], ids[0]); // 複製は別の識別子を持つ
    const LevelNs::ObjectInstance& dup = lv.objects[1];
    EXPECT_FLOAT_EQ(dup.positionX, 3.0f);
    ASSERT_EQ(dup.components.size(), 2u);
    EXPECT_EQ(dup.components[0].typeName, "PoleComponent");
    EXPECT_EQ(dup.components[1].typeName, "BoxCollider");
    ASSERT_EQ(dup.components[1].fields.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<float>(dup.components[1].fields[0].value));
    EXPECT_FLOAT_EQ(std::get<float>(dup.components[1].fields[0].value), 1.5f);

    cmd.Undo(t);
    ASSERT_EQ(lv.objects.size(), 1u);
    EXPECT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], id);
}

TEST(ComponentCommand, DuplicateObjectRedoReusesSameId)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(MakeObject());
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};
    LevelNs::ResetEditIds(t);

    const std::uint32_t srcId = LevelNs::IdAt(t, 0);
    EditorNs::DuplicateObjectCommand cmd(srcId);

    cmd.Do(t);
    ASSERT_EQ(ids.size(), 2u);
    const std::uint32_t dupId = ids[1];
    EXPECT_EQ(next, 2u);

    cmd.Undo(t);
    ASSERT_EQ(lv.objects.size(), 1u);

    cmd.Do(t); // redo: 同じ識別子を再利用し next は増やさない
    ASSERT_EQ(lv.objects.size(), 2u);
    EXPECT_EQ(ids[1], dupId);
    EXPECT_EQ(next, 2u);
}

TEST(ComponentCommand, CommandsOnUnknownIdAreNoOp)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(MakeObject());
    lv.objects[0].components.push_back(MakeBoxCollider(1.0f));
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};
    LevelNs::ResetEditIds(t);

    const std::uint32_t unknownId = 999u;

    EditorNs::AddComponentCommand add(unknownId, LevelNs::ComponentData{"PoleComponent"});
    add.Do(t);
    add.Undo(t);
    EXPECT_EQ(lv.objects[0].components.size(), 1u); // 対象が居ないので増減しない

    EditorNs::RemoveComponentCommand remove(unknownId, 0);
    remove.Do(t);
    remove.Undo(t);
    EXPECT_EQ(lv.objects[0].components.size(), 1u);

    EditorNs::DuplicateObjectCommand dup(unknownId);
    dup.Do(t);
    EXPECT_EQ(lv.objects.size(), 1u);
    dup.Undo(t);
    EXPECT_EQ(lv.objects.size(), 1u);
}

TEST(ComponentCommand, RemoveLastComponentIsRefusedToAvoidGhost)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(MakeObject());
    lv.objects[0].components.push_back(LevelNs::ComponentData{"MeshRendererComponent"});
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};
    LevelNs::ResetEditIds(t);

    const std::uint32_t id = LevelNs::IdAt(t, 0);
    EditorNs::RemoveComponentCommand cmd(id, 0); // 唯一の component を消そうとする

    cmd.Do(t); // 空構成は build で不可視ゴーストになるので除去を拒む
    ASSERT_EQ(lv.objects[0].components.size(), 1u);
    EXPECT_EQ(lv.objects[0].components[0].typeName, "MeshRendererComponent");

    cmd.Undo(t); // 退避が無いので Undo も何もしない
    ASSERT_EQ(lv.objects[0].components.size(), 1u);
    EXPECT_EQ(lv.objects[0].components[0].typeName, "MeshRendererComponent");
}

TEST(ComponentCommand, SetObjectComponentsReplacesWholeListUndoRestores)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(MakeObject());
    lv.objects[0].components.push_back(LevelNs::ComponentData{"MeshRendererComponent"});
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};
    LevelNs::ResetEditIds(t);

    const std::uint32_t id = LevelNs::IdAt(t, 0);
    std::vector<LevelNs::ComponentData> replacement;
    replacement.push_back(LevelNs::ComponentData{"MeshRendererComponent"});
    replacement.push_back(MakeBoxCollider(0.5f));
    replacement.push_back(LevelNs::ComponentData{"PoleComponent"});
    EditorNs::SetObjectComponentsCommand cmd(id, replacement);

    cmd.Do(t);
    ASSERT_EQ(lv.objects[0].components.size(), 3u);
    EXPECT_EQ(lv.objects[0].components[1].typeName, "BoxCollider");
    EXPECT_EQ(lv.objects[0].components[2].typeName, "PoleComponent");

    cmd.Undo(t); // 置換前の 1 件だけの一覧へ戻る
    ASSERT_EQ(lv.objects[0].components.size(), 1u);
    EXPECT_EQ(lv.objects[0].components[0].typeName, "MeshRendererComponent");

    cmd.Do(t); // redo: 退避した旧一覧を上書きしても同じ置換結果になる
    ASSERT_EQ(lv.objects[0].components.size(), 3u);
    EXPECT_EQ(lv.objects[0].components[2].typeName, "PoleComponent");

    cmd.Undo(t); // 再び置換前の 1 件へ戻る
    ASSERT_EQ(lv.objects[0].components.size(), 1u);
    EXPECT_EQ(lv.objects[0].components[0].typeName, "MeshRendererComponent");
}
