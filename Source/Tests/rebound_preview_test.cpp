#include "Game/Level/ReboundPreview.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

// 反動の矢印を画面の線に組む計算を縛る。太い矢印は当て方で決まる向き、細い矢印は曲げられる先

namespace
{
    using GL::Level::BuildReboundArrowLines;
    using GL::Level::ReboundArrowLine;
    using GL::Level::ReboundPreviewArrows;
    using GL::Level::ReboundPreviewDesc;

    const NS::Size2D k_Target{1280, 720};

    // 手前の (0, 1, -5) から奥の (0, 1, 0) を見るカメラ
    NS::Matrix ViewProjection()
    {
        const NS::Matrix view =
            NS::Matrix::CreateLookAt(NS::Vector3{0.0f, 1.0f, -5.0f}, NS::Vector3{0.0f, 1.0f, 0.0f}, NS::Vector3::Up);
        return view * NS::Matrix::CreatePerspectiveFieldOfView(1.0f, 1280.0f / 720.0f, 0.1f, 100.0f);
    }
} // namespace

TEST(ReboundPreviewTest, AssignedArrowIsDrawnLastAndThickerThanTheChoices)
{
    const ReboundPreviewArrows arrows{.origin = NS::Vector3{0.0f, 1.0f, 0.0f},
                                      .assigned = NS::Vector3{1.0f, 0.0f, 0.0f},
                                      .choices = {NS::Vector3{0.0f, 1.0f, 0.0f}}};
    const ReboundPreviewDesc desc{};
    std::vector<ReboundArrowLine> lines{};
    ASSERT_TRUE(BuildReboundArrowLines(arrows, ViewProjection(), k_Target, desc, lines));
    // 矢印ごとに軸と矢じりの 2 本
    ASSERT_EQ(lines.size(), 6u);

    const ReboundArrowLine& choiceShaft = lines[0];
    EXPECT_NEAR(choiceShaft.startX, 640.0f, 0.5f);
    EXPECT_NEAR(choiceShaft.startY, 360.0f, 0.5f);
    EXPECT_LT(choiceShaft.endY, choiceShaft.startY) << "上へ曲げる先は画面の上へ伸びる";
    EXPECT_FLOAT_EQ(choiceShaft.thickness, desc.choiceThickness);

    const ReboundArrowLine& assignedShaft = lines[3];
    EXPECT_NEAR(assignedShaft.startX, 640.0f, 0.5f);
    EXPECT_NEAR(assignedShaft.endY, 360.0f, 0.5f) << "横へ返る向きは画面の横へ伸びる";
    EXPECT_GT(std::abs(assignedShaft.endX - assignedShaft.startX), 1.0f);
    for (int i = 3; i < 6; ++i)
    {
        EXPECT_FLOAT_EQ(lines[i].thickness, desc.assignedThickness);
        EXPECT_FLOAT_EQ(lines[i].color.w, desc.assignedColor.w);
    }
    // 矢じりの線は矢じりの先から始まる
    EXPECT_FLOAT_EQ(lines[4].startX, assignedShaft.endX);
    EXPECT_FLOAT_EQ(lines[5].startY, assignedShaft.endY);
}

TEST(ReboundPreviewTest, ThicknessFollowsTheScreenHeight)
{
    const ReboundPreviewArrows arrows{.origin = NS::Vector3{0.0f, 1.0f, 0.0f}, .assigned = NS::Vector3{1.0f, 0.0f, 0.0f}};
    const ReboundPreviewDesc desc{};
    std::vector<ReboundArrowLine> lines{};
    ASSERT_TRUE(BuildReboundArrowLines(arrows, ViewProjection(), NS::Size2D{2560, 1440}, desc, lines));
    ASSERT_FALSE(lines.empty());
    EXPECT_FLOAT_EQ(lines[0].thickness, desc.assignedThickness * 2.0f);
}

TEST(ReboundPreviewTest, ArrowsBehindTheCameraOrPointingAtItAreSkipped)
{
    std::vector<ReboundArrowLine> lines{};
    const ReboundPreviewArrows behind{.origin = NS::Vector3{0.0f, 1.0f, -8.0f}, .assigned = NS::Vector3{1.0f, 0.0f, 0.0f}};
    ASSERT_TRUE(BuildReboundArrowLines(behind, ViewProjection(), k_Target, ReboundPreviewDesc{}, lines));
    EXPECT_TRUE(lines.empty());

    const ReboundPreviewArrows atCamera{.origin = NS::Vector3{0.0f, 1.0f, 0.0f}, .assigned = NS::Vector3{0.0f, 0.0f, -1.0f}};
    ASSERT_TRUE(BuildReboundArrowLines(atCamera, ViewProjection(), k_Target, ReboundPreviewDesc{}, lines));
    EXPECT_TRUE(lines.empty());
}

TEST(ReboundPreviewTest, InvalidDescIsRejectedWithoutWriting)
{
    const ReboundPreviewArrows arrows{.origin = NS::Vector3{0.0f, 1.0f, 0.0f}, .assigned = NS::Vector3{1.0f, 0.0f, 0.0f}};
    std::vector<ReboundArrowLine> lines(1);
    ReboundPreviewDesc desc{};
    desc.referenceHeight = 0.0f;
    EXPECT_FALSE(BuildReboundArrowLines(arrows, ViewProjection(), k_Target, desc, lines));
    EXPECT_EQ(lines.size(), 1u);
    EXPECT_FALSE(BuildReboundArrowLines(arrows, ViewProjection(), NS::Size2D{0, 0}, ReboundPreviewDesc{}, lines));
    EXPECT_EQ(lines.size(), 1u);
}
