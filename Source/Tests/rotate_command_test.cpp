#include "Editor/Undo/RotateCommand.h"
#include "GameCore/Level/LevelData.h"

#include <gtest/gtest.h>

namespace EditorNs = NS::Editor;
namespace LevelNs = NS::GameCore::Level;

TEST(RotateCommandTest, DoIncrementsRotation)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));
    EditorNs::RotateCommand cmd(0, 0, 0, +1);
    cmd.Do(lv);
    const std::size_t idx = LevelNs::FindObjectAtCell(lv, 0, 0, 0);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::CellRotationStep(lv.objects[idx]), 1u);
}

TEST(RotateCommandTest, FourDoesCycleBackToZero)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));
    EditorNs::RotateCommand cmd1(0, 0, 0, +1);
    EditorNs::RotateCommand cmd2(0, 0, 0, +1);
    EditorNs::RotateCommand cmd3(0, 0, 0, +1);
    EditorNs::RotateCommand cmd4(0, 0, 0, +1);
    cmd1.Do(lv);
    cmd2.Do(lv);
    cmd3.Do(lv);
    cmd4.Do(lv);
    const std::size_t idx = LevelNs::FindObjectAtCell(lv, 0, 0, 0);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::CellRotationStep(lv.objects[idx]), 0u);
}

TEST(RotateCommandTest, NegativeDeltaWrapsToThree)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));
    EditorNs::RotateCommand cmd(0, 0, 0, -1);
    cmd.Do(lv);
    const std::size_t idx = LevelNs::FindObjectAtCell(lv, 0, 0, 0);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::CellRotationStep(lv.objects[idx]), 3u);
}

TEST(RotateCommandTest, UndoRestoresPreviousRotation)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 2));
    EditorNs::RotateCommand cmd(0, 0, 0, +1);
    cmd.Do(lv);
    std::size_t idx = LevelNs::FindObjectAtCell(lv, 0, 0, 0);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::CellRotationStep(lv.objects[idx]), 3u);
    cmd.Undo(lv);
    idx = LevelNs::FindObjectAtCell(lv, 0, 0, 0);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::CellRotationStep(lv.objects[idx]), 2u);
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

// SetCellRotationStep が回転値 0..3 を Y 軸 0/90/180/270° の絶対 quaternion に焼くことを検証する
// CellRotationStep との往復だけでは同じ定数を両向きに使うため、値が半分 / 倍 / 符号違いでも通ってしまう
// 期待値は独立した π/2 リテラルから組み、回転定数を触ったときに落ちる回帰ガードにする
TEST(CellRotationTest, SetCellRotationStepBakesQuarterTurns)
{
    constexpr float kHalfPi = 1.57079632679489661923f;
    for (std::uint8_t step = 0; step < 4; ++step)
    {
        LevelNs::ObjectInstance obj;
        LevelNs::SetCellRotationStep(obj, step);
        const NS::Math::Quaternion expected =
            NS::Math::Quaternion::CreateFromYawPitchRoll(static_cast<float>(step) * kHalfPi, 0.0f, 0.0f);
        EXPECT_NEAR(obj.rotationX, expected.x, 1e-5f) << "step " << static_cast<int>(step);
        EXPECT_NEAR(obj.rotationY, expected.y, 1e-5f) << "step " << static_cast<int>(step);
        EXPECT_NEAR(obj.rotationZ, expected.z, 1e-5f) << "step " << static_cast<int>(step);
        EXPECT_NEAR(obj.rotationW, expected.w, 1e-5f) << "step " << static_cast<int>(step);
    }
}
