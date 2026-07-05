#include "Editor/Undo/RotateCommand.h"
#include "GameCore/Level/LevelData.h"

#include <gtest/gtest.h>

namespace EditorNs = NS::Editor;
namespace LevelNs = NS::GameCore::Level;

TEST(RotateCommandTest, DoIncrementsRotation)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 0));
    EditorNs::RotateCommand cmd(0, 0, 0, +1);
    cmd.Do(lv);
    const std::size_t idx = LevelNs::FindGridObjectAtCell(lv, 0, 0, 0);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[idx]), 1u);
}

TEST(RotateCommandTest, FourDoesCycleBackToZero)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 0));
    EditorNs::RotateCommand cmd1(0, 0, 0, +1);
    EditorNs::RotateCommand cmd2(0, 0, 0, +1);
    EditorNs::RotateCommand cmd3(0, 0, 0, +1);
    EditorNs::RotateCommand cmd4(0, 0, 0, +1);
    cmd1.Do(lv);
    cmd2.Do(lv);
    cmd3.Do(lv);
    cmd4.Do(lv);
    const std::size_t idx = LevelNs::FindGridObjectAtCell(lv, 0, 0, 0);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[idx]), 0u);
}

TEST(RotateCommandTest, NegativeDeltaWrapsToThree)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 0));
    EditorNs::RotateCommand cmd(0, 0, 0, -1);
    cmd.Do(lv);
    const std::size_t idx = LevelNs::FindGridObjectAtCell(lv, 0, 0, 0);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[idx]), 3u);
}

TEST(RotateCommandTest, UndoRestoresPreviousRotation)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 2));
    EditorNs::RotateCommand cmd(0, 0, 0, +1);
    cmd.Do(lv);
    std::size_t idx = LevelNs::FindGridObjectAtCell(lv, 0, 0, 0);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[idx]), 3u);
    cmd.Undo(lv);
    idx = LevelNs::FindGridObjectAtCell(lv, 0, 0, 0);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[idx]), 2u);
}

TEST(RotateCommandTest, NonExistentCellIsNoOp)
{
    LevelNs::LevelData lv;
    const auto before = lv.ComputeCrc32();
    EditorNs::RotateCommand cmd(7, 7, 7, +1);
    cmd.Do(lv);
    cmd.Undo(lv);
    EXPECT_EQ(lv.ComputeCrc32(), before);
}
