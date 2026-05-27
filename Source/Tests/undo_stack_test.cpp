#include "Game/Level/LevelData.h"
#include "Game/Undo/PlaceCommand.h"
#include "Game/Undo/UndoStack.h"

#include <gtest/gtest.h>

#include <memory>

namespace UndoNs = NS::Game::Undo;
namespace LevelNs = NS::Game::Level;

TEST(UndoStackTest, EmptyStackUndoRedoReturnFalse)
{
    UndoNs::UndoStack stack;
    LevelNs::LevelData lv;
    EXPECT_FALSE(stack.Undo(lv));
    EXPECT_FALSE(stack.Redo(lv));
}

TEST(UndoStackTest, PushExecutesDoAndStoresInUndoStack)
{
    UndoNs::UndoStack stack;
    LevelNs::LevelData lv;
    stack.Push(std::make_unique<UndoNs::PlaceCommand>(0, 0, 0, 1, 0), lv);
    EXPECT_EQ(stack.UndoSize(), 1u);
    EXPECT_EQ(stack.RedoSize(), 0u);
    EXPECT_EQ(lv.blocks.size(), 1u);
}

TEST(UndoStackTest, UndoRedoRoundTripPreservesState)
{
    UndoNs::UndoStack stack;
    LevelNs::LevelData lv;
    const auto before = lv.ComputeCrc32();

    stack.Push(std::make_unique<UndoNs::PlaceCommand>(0, 0, 0, 1, 0), lv);
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
    UndoNs::UndoStack stack;
    LevelNs::LevelData lv;
    for (std::size_t i = 0; i < UndoNs::UndoStack::kMaxOps + 5; ++i)
    {
        stack.Push(std::make_unique<UndoNs::PlaceCommand>(static_cast<std::int16_t>(i), 0, 0, 1, 0), lv);
    }
    EXPECT_EQ(stack.UndoSize(), UndoNs::UndoStack::kMaxOps);
}

TEST(UndoStackTest, PushAfterUndoClearsRedoStack)
{
    UndoNs::UndoStack stack;
    LevelNs::LevelData lv;
    stack.Push(std::make_unique<UndoNs::PlaceCommand>(0, 0, 0, 1, 0), lv);
    stack.Undo(lv);
    EXPECT_EQ(stack.RedoSize(), 1u);

    stack.Push(std::make_unique<UndoNs::PlaceCommand>(1, 0, 0, 2, 0), lv);
    EXPECT_EQ(stack.RedoSize(), 0u);
}

TEST(UndoStackTest, ClearEmptiesBothStacks)
{
    UndoNs::UndoStack stack;
    LevelNs::LevelData lv;
    stack.Push(std::make_unique<UndoNs::PlaceCommand>(0, 0, 0, 1, 0), lv);
    stack.Push(std::make_unique<UndoNs::PlaceCommand>(1, 0, 0, 2, 0), lv);
    stack.Undo(lv);

    stack.Clear();
    EXPECT_EQ(stack.UndoSize(), 0u);
    EXPECT_EQ(stack.RedoSize(), 0u);
    EXPECT_EQ(stack.EstimatedBytes(), 0u);
}
