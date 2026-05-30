#include "Game/Editor/BlockRegistry.h"

#include <gtest/gtest.h>

namespace EditorNs = NS::Game::Editor;

TEST(BlockRegistry, SlopeAndSolidAreRotatable)
{
    EXPECT_TRUE(EditorNs::IsRotatableBlock(EditorNs::kBlockIdSlope45));
    EXPECT_TRUE(EditorNs::IsRotatableBlock(EditorNs::kBlockIdSlope15));
    EXPECT_TRUE(EditorNs::IsRotatableBlock(EditorNs::kBlockIdSolid));
}

TEST(BlockRegistry, NonOrientableBlocksAreNotRotatable)
{
    EXPECT_FALSE(EditorNs::IsRotatableBlock(EditorNs::kBlockIdPole));
    EXPECT_FALSE(EditorNs::IsRotatableBlock(EditorNs::kBlockIdFence));
    EXPECT_FALSE(EditorNs::IsRotatableBlock(EditorNs::kBlockIdWater));
    EXPECT_FALSE(EditorNs::IsRotatableBlock(EditorNs::kBlockIdDecoration));
    EXPECT_FALSE(EditorNs::IsRotatableBlock(EditorNs::kBlockIdCoin));
}

TEST(BlockRegistry, RotationToYawIsQuarterTurns)
{
    // rotation 0/1/2/3 が 0/90/180/270° へ対応することを確認する。
    constexpr float kPi = 3.14159265358979323846f;
    EXPECT_NEAR(EditorNs::BlockRotationToYaw(0), 0.0f, 1e-4f);
    EXPECT_NEAR(EditorNs::BlockRotationToYaw(1), kPi * 0.5f, 1e-4f);
    EXPECT_NEAR(EditorNs::BlockRotationToYaw(2), kPi, 1e-4f);
    EXPECT_NEAR(EditorNs::BlockRotationToYaw(3), kPi * 1.5f, 1e-4f);
}
