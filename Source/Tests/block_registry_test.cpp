#include "Game/Blocks/BlockRegistry.h"

#include <gtest/gtest.h>


TEST(BlockRegistry, SlopeAndSolidAreRotatable)
{
    EXPECT_TRUE(NS::Game::Blocks::IsRotatableBlock(NS::Game::Blocks::kBlockIdSlope45));
    EXPECT_TRUE(NS::Game::Blocks::IsRotatableBlock(NS::Game::Blocks::kBlockIdSlope15));
    EXPECT_TRUE(NS::Game::Blocks::IsRotatableBlock(NS::Game::Blocks::kBlockIdSolid));
}

TEST(BlockRegistry, NonOrientableBlocksAreNotRotatable)
{
    EXPECT_FALSE(NS::Game::Blocks::IsRotatableBlock(NS::Game::Blocks::kBlockIdPole));
    EXPECT_FALSE(NS::Game::Blocks::IsRotatableBlock(NS::Game::Blocks::kBlockIdWater));
    EXPECT_FALSE(NS::Game::Blocks::IsRotatableBlock(NS::Game::Blocks::kBlockIdDecoration));
    EXPECT_FALSE(NS::Game::Blocks::IsRotatableBlock(NS::Game::Blocks::kBlockIdCoin));
}

TEST(BlockRegistry, NextSlopeBlockCyclesAngles)
{
    EXPECT_EQ(NS::Game::Blocks::NextSlopeBlock(NS::Game::Blocks::kBlockIdSlope45), NS::Game::Blocks::kBlockIdSlope30);
    EXPECT_EQ(NS::Game::Blocks::NextSlopeBlock(NS::Game::Blocks::kBlockIdSlope30), NS::Game::Blocks::kBlockIdSlope22);
    EXPECT_EQ(NS::Game::Blocks::NextSlopeBlock(NS::Game::Blocks::kBlockIdSlope22), NS::Game::Blocks::kBlockIdSlope15);
    EXPECT_EQ(NS::Game::Blocks::NextSlopeBlock(NS::Game::Blocks::kBlockIdSlope15), NS::Game::Blocks::kBlockIdSlope45);
}

TEST(BlockRegistry, NextSlopeBlockOnNonSlopeIsUnchanged)
{
    EXPECT_EQ(NS::Game::Blocks::NextSlopeBlock(NS::Game::Blocks::kBlockIdSolid), NS::Game::Blocks::kBlockIdSolid);
    EXPECT_EQ(NS::Game::Blocks::NextSlopeBlock(NS::Game::Blocks::kBlockIdPole), NS::Game::Blocks::kBlockIdPole);
}

TEST(BlockRegistry, RotationToYawIsQuarterTurns)
{
    // rotation 0/1/2/3 が 0/90/180/270° へ対応することを確認する
    constexpr float kPi = 3.14159265358979323846f;
    EXPECT_NEAR(NS::Game::Blocks::BlockRotationToYaw(0), 0.0f, 1e-4f);
    EXPECT_NEAR(NS::Game::Blocks::BlockRotationToYaw(1), kPi * 0.5f, 1e-4f);
    EXPECT_NEAR(NS::Game::Blocks::BlockRotationToYaw(2), kPi, 1e-4f);
    EXPECT_NEAR(NS::Game::Blocks::BlockRotationToYaw(3), kPi * 1.5f, 1e-4f);
}
