#include "Editor/Undo/PlaceCommand.h"
#include "Editor/Undo/TransformCommand.h"
#include "Editor/Undo/UndoStack.h"
#include "Game/Level/LevelData.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

namespace EditorNs = NS::Editor;
namespace LevelNs = NS::Game::Level;

TEST(UndoStackTest, EmptyStackUndoRedoReturnFalse)
{
    EditorNs::UndoStack stack;
    LevelNs::LevelData lv;
    EXPECT_FALSE(stack.Undo(lv));
    EXPECT_FALSE(stack.Redo(lv));
}

TEST(UndoStackTest, PushExecutesDoAndStoresInUndoStack)
{
    EditorNs::UndoStack stack;
    LevelNs::LevelData lv;
    stack.Push(std::make_unique<EditorNs::PlaceCommand>(LevelNs::MakeCellObject(0, 0, 0, 0), 0, 0, 0, 0), lv);
    EXPECT_EQ(stack.UndoSize(), 1u);
    EXPECT_EQ(stack.RedoSize(), 0u);
    EXPECT_EQ(lv.objects.size(), 1u);
}

TEST(UndoStackTest, UndoRedoRoundTripPreservesState)
{
    EditorNs::UndoStack stack;
    LevelNs::LevelData lv;
    const auto before = lv.ComputeCrc32();

    stack.Push(std::make_unique<EditorNs::PlaceCommand>(LevelNs::MakeCellObject(0, 0, 0, 0), 0, 0, 0, 0), lv);
    const auto afterPush = lv.ComputeCrc32();

    EXPECT_TRUE(stack.Undo(lv));
    EXPECT_EQ(lv.ComputeCrc32(), before);
    EXPECT_EQ(stack.UndoSize(), 0u);
    EXPECT_EQ(stack.RedoSize(), 1u);

    EXPECT_TRUE(stack.Redo(lv));
    EXPECT_EQ(lv.ComputeCrc32(), afterPush);
    EXPECT_EQ(stack.UndoSize(), 1u);
    EXPECT_EQ(stack.RedoSize(), 0u);
}

TEST(UndoStackTest, MaxOpsCapPopsOldest)
{
    EditorNs::UndoStack stack;
    LevelNs::LevelData lv;
    for (std::size_t i = 0; i < EditorNs::UndoStack::kMaxOps + 5; ++i)
    {
        stack.Push(std::make_unique<EditorNs::PlaceCommand>(
                       LevelNs::MakeCellObject(0, 0, 0, 0), static_cast<std::int16_t>(i), 0, 0, 0),
                   lv);
    }
    EXPECT_EQ(stack.UndoSize(), EditorNs::UndoStack::kMaxOps);
}

TEST(UndoStackTest, PushAfterUndoClearsRedoStack)
{
    EditorNs::UndoStack stack;
    LevelNs::LevelData lv;
    stack.Push(std::make_unique<EditorNs::PlaceCommand>(LevelNs::MakeCellObject(0, 0, 0, 0), 0, 0, 0, 0), lv);
    stack.Undo(lv);
    EXPECT_EQ(stack.RedoSize(), 1u);

    stack.Push(std::make_unique<EditorNs::PlaceCommand>(LevelNs::MakeCellObject(0, 0, 0, 0), 1, 0, 0, 0), lv);
    EXPECT_EQ(stack.RedoSize(), 0u);
}

TEST(UndoStackTest, ClearEmptiesBothStacks)
{
    EditorNs::UndoStack stack;
    LevelNs::LevelData lv;
    stack.Push(std::make_unique<EditorNs::PlaceCommand>(LevelNs::MakeCellObject(0, 0, 0, 0), 0, 0, 0, 0), lv);
    stack.Push(std::make_unique<EditorNs::PlaceCommand>(LevelNs::MakeCellObject(0, 0, 0, 0), 1, 0, 0, 0), lv);
    stack.Undo(lv);

    stack.Clear();
    EXPECT_EQ(stack.UndoSize(), 0u);
    EXPECT_EQ(stack.RedoSize(), 0u);
    EXPECT_EQ(stack.EstimatedBytes(), 0u);
}

TEST(UndoStackTest, InterleavedGridAndTransformUndoInLifoOrder)
{
    EditorNs::UndoStack stack;
    LevelNs::LevelData lv;

    // grid block を置く (append、 永続 id 採番)
    stack.Push(std::make_unique<EditorNs::PlaceCommand>(LevelNs::MakeCellObject(0, 0, 0, 0), 0, 0, 0, 0), lv);
    ASSERT_EQ(lv.objects.size(), 1u);
    const std::uint32_t id0 = lv.objects[0].objectId;
    ASSERT_NE(id0, LevelNs::kNoObjectId);

    // 同じ object を移動した想定の full-instance 変形
    LevelNs::ObjectInstance before = lv.objects[0];
    LevelNs::ObjectInstance after = before;
    after.positionX = 9.0f;
    stack.Push(std::make_unique<EditorNs::TransformCommand>(id0, before, after), lv);
    EXPECT_FLOAT_EQ(lv.objects[0].positionX, 9.0f);

    // LIFO: 先に変形を戻すと元位置へ復帰する
    ASSERT_TRUE(stack.Undo(lv));
    EXPECT_FLOAT_EQ(lv.objects[0].positionX, 0.0f);

    // 次に配置を戻すと空になる
    ASSERT_TRUE(stack.Undo(lv));
    EXPECT_TRUE(lv.objects.empty());

    // redo 2 回で最終状態へ戻る
    ASSERT_TRUE(stack.Redo(lv));
    ASSERT_TRUE(stack.Redo(lv));
    ASSERT_EQ(lv.objects.size(), 1u);
    EXPECT_FLOAT_EQ(lv.objects[0].positionX, 9.0f);
}
