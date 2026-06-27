#include "Editor/Undo/PlaceCommand.h"
#include "Editor/Undo/TransformCommand.h"
#include "Editor/Undo/UndoStack.h"
#include "Game/Level/EditTarget.h"
#include "Game/Level/LevelData.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace EditorNs = NS::Editor;
namespace LevelNs = NS::Game::Level;

TEST(UndoStackTest, EmptyStackUndoRedoReturnFalse)
{
    EditorNs::UndoStack stack;
    LevelNs::LevelData lv;
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};
    EXPECT_FALSE(stack.Undo(t));
    EXPECT_FALSE(stack.Redo(t));
}

TEST(UndoStackTest, PushExecutesDoAndStoresInUndoStack)
{
    EditorNs::UndoStack stack;
    LevelNs::LevelData lv;
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};
    stack.Push(std::make_unique<EditorNs::PlaceCommand>(LevelNs::MakeGridObject(0, 0, 0, 1, 0), 0, 0, 0, 0), t);
    EXPECT_EQ(stack.UndoSize(), 1u);
    EXPECT_EQ(stack.RedoSize(), 0u);
    EXPECT_EQ(lv.objects.size(), 1u);
    EXPECT_EQ(ids.size(), 1u);
}

TEST(UndoStackTest, UndoRedoRoundTripPreservesState)
{
    EditorNs::UndoStack stack;
    LevelNs::LevelData lv;
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};
    const auto before = lv.ComputeCrc32();

    stack.Push(std::make_unique<EditorNs::PlaceCommand>(LevelNs::MakeGridObject(0, 0, 0, 1, 0), 0, 0, 0, 0), t);
    const auto afterPush = lv.ComputeCrc32();

    EXPECT_TRUE(stack.Undo(t));
    EXPECT_EQ(lv.ComputeCrc32(), before);
    EXPECT_EQ(stack.UndoSize(), 0u);
    EXPECT_EQ(stack.RedoSize(), 1u);

    EXPECT_TRUE(stack.Redo(t));
    EXPECT_EQ(lv.ComputeCrc32(), afterPush);
    EXPECT_EQ(stack.UndoSize(), 1u);
    EXPECT_EQ(stack.RedoSize(), 0u);
}

TEST(UndoStackTest, MaxOpsCapPopsOldest)
{
    EditorNs::UndoStack stack;
    LevelNs::LevelData lv;
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};
    for (std::size_t i = 0; i < EditorNs::UndoStack::kMaxOps + 5; ++i)
    {
        stack.Push(std::make_unique<EditorNs::PlaceCommand>(
                       LevelNs::MakeGridObject(0, 0, 0, 1, 0), static_cast<std::int16_t>(i), 0, 0, 0),
                   t);
    }
    EXPECT_EQ(stack.UndoSize(), EditorNs::UndoStack::kMaxOps);
}

TEST(UndoStackTest, PushAfterUndoClearsRedoStack)
{
    EditorNs::UndoStack stack;
    LevelNs::LevelData lv;
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};
    stack.Push(std::make_unique<EditorNs::PlaceCommand>(LevelNs::MakeGridObject(0, 0, 0, 1, 0), 0, 0, 0, 0), t);
    stack.Undo(t);
    EXPECT_EQ(stack.RedoSize(), 1u);

    stack.Push(std::make_unique<EditorNs::PlaceCommand>(LevelNs::MakeGridObject(0, 0, 0, 2, 0), 1, 0, 0, 0), t);
    EXPECT_EQ(stack.RedoSize(), 0u);
}

TEST(UndoStackTest, ClearEmptiesBothStacks)
{
    EditorNs::UndoStack stack;
    LevelNs::LevelData lv;
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};
    stack.Push(std::make_unique<EditorNs::PlaceCommand>(LevelNs::MakeGridObject(0, 0, 0, 1, 0), 0, 0, 0, 0), t);
    stack.Push(std::make_unique<EditorNs::PlaceCommand>(LevelNs::MakeGridObject(0, 0, 0, 2, 0), 1, 0, 0, 0), t);
    stack.Undo(t);

    stack.Clear();
    EXPECT_EQ(stack.UndoSize(), 0u);
    EXPECT_EQ(stack.RedoSize(), 0u);
    EXPECT_EQ(stack.EstimatedBytes(), 0u);
}

TEST(UndoStackTest, InterleavedGridAndTransformUndoInLifoOrder)
{
    EditorNs::UndoStack stack;
    LevelNs::LevelData lv;
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};

    // grid block を置く (append、 id 採番)
    stack.Push(std::make_unique<EditorNs::PlaceCommand>(LevelNs::MakeGridObject(0, 0, 0, 1, 0), 0, 0, 0, 0), t);
    ASSERT_EQ(lv.objects.size(), 1u);
    ASSERT_EQ(ids.size(), 1u);
    const std::uint32_t id0 = ids[0];

    // 同じ object を昇格 + 移動した想定の full-instance 変形
    LevelNs::ObjectInstance before = lv.objects[0];
    LevelNs::ObjectInstance after = before;
    after.positionX = 9.0f;
    after.flags = static_cast<std::uint8_t>(before.flags & ~LevelNs::kObjectFlagGridAligned);
    stack.Push(std::make_unique<EditorNs::TransformCommand>(id0, before, after), t);
    EXPECT_FLOAT_EQ(lv.objects[0].positionX, 9.0f);

    // LIFO: 先に変形を戻すと grid 状態 + 元位置へ復帰する
    ASSERT_TRUE(stack.Undo(t));
    EXPECT_FLOAT_EQ(lv.objects[0].positionX, 0.0f);
    EXPECT_EQ(lv.objects[0].flags & LevelNs::kObjectFlagGridAligned, LevelNs::kObjectFlagGridAligned);

    // 次に配置を戻すと空になる
    ASSERT_TRUE(stack.Undo(t));
    EXPECT_TRUE(lv.objects.empty());
    EXPECT_TRUE(ids.empty());

    // redo 2 回で最終状態へ戻る
    ASSERT_TRUE(stack.Redo(t));
    ASSERT_TRUE(stack.Redo(t));
    ASSERT_EQ(lv.objects.size(), 1u);
    EXPECT_FLOAT_EQ(lv.objects[0].positionX, 9.0f);
}
