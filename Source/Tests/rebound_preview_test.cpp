#include "Game/Level/ReboundPreview.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

// 反動の矢印を世界の座標の線に組む計算を縛る。太い矢印は当て方で決まる向き、細い矢印は曲げられる先

namespace
{
    using GL::Level::BuildReboundArrowLines;
    using GL::Level::ReboundPreviewArrows;
    using GL::Level::ReboundPreviewDesc;

    void ExpectNear(const NS::Vector3& actual, const NS::Vector3& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, 0.0001f);
        EXPECT_NEAR(actual.y, expected.y, 0.0001f);
        EXPECT_NEAR(actual.z, expected.z, 0.0001f);
    }
} // namespace

// 矢印ごとに軸と矢じりの 2 本を世界の座標で組み、当て方の向きを最後に組んで上に重ねる
TEST(ReboundPreviewTest, AssignedArrowIsWrittenLastAndThickerThanTheChoices)
{
    const NS::Vector3 origin{0.0f, 1.0f, 0.0f};
    const ReboundPreviewArrows arrows{
        .origin = origin, .assigned = NS::Vector3{1.0f, 0.0f, 0.0f}, .choices = {NS::Vector3{0.0f, 1.0f, 0.0f}}};
    const ReboundPreviewDesc desc{};
    std::vector<NS::WorldLine> lines{};
    ASSERT_TRUE(BuildReboundArrowLines(arrows, desc, lines));
    ASSERT_EQ(lines.size(), 6u);

    const NS::WorldLine& choiceShaft = lines[0];
    ExpectNear(choiceShaft.start, origin);
    ExpectNear(choiceShaft.end, origin + NS::Vector3{0.0f, desc.choiceLength, 0.0f});
    EXPECT_FLOAT_EQ(choiceShaft.thickness, desc.choiceThickness / desc.referenceHeight);

    const NS::WorldLine& assignedShaft = lines[3];
    const NS::Vector3 tip = origin + NS::Vector3{desc.assignedLength, 0.0f, 0.0f};
    ExpectNear(assignedShaft.end, tip);
    for (std::size_t i = 3; i < 6; ++i)
    {
        EXPECT_FLOAT_EQ(lines[i].thickness, desc.assignedThickness / desc.referenceHeight);
        EXPECT_FLOAT_EQ(lines[i].color.w, desc.assignedColor.w);
    }
    // 矢じりの 2 本は矢じりの先から軸を戻る側へ、左右対称に開く
    for (std::size_t i = 4; i < 6; ++i)
    {
        ExpectNear(lines[i].start, tip);
        EXPECT_LT(lines[i].end.x, tip.x);
        EXPECT_NEAR((lines[i].end - tip).Length(), desc.assignedLength * desc.headLengthRatio, 0.0001f);
    }
    EXPECT_NEAR(lines[4].end.z, -lines[5].end.z, 0.0001f);
    EXPECT_GT(std::abs(lines[4].end.z), 0.0001f);
}

TEST(ReboundPreviewTest, InvalidDescIsRejectedWithoutWriting)
{
    const ReboundPreviewArrows arrows{.origin = NS::Vector3{0.0f, 1.0f, 0.0f}, .assigned = NS::Vector3{1.0f, 0.0f, 0.0f}};
    std::vector<NS::WorldLine> lines(1);
    ReboundPreviewDesc desc{};
    desc.referenceHeight = 0.0f;
    EXPECT_FALSE(BuildReboundArrowLines(arrows, desc, lines));
    EXPECT_EQ(lines.size(), 1u);
}
