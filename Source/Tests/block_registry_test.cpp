#include "Game/Blocks/BlockRegistry.h"
#include "Game/Blocks/BuildPlacedObject.h"
#include "Game/Level/LevelData.h"

#include <cstdint>

#include <gtest/gtest.h>

namespace
{
    // grid 種別を実 component へ起こした配置物。 回転可否は component から導く
    NS::Game::Level::ObjectInstance MakeGridKind(std::uint16_t kind)
    {
        NS::Game::Level::ObjectInstance object{};
        object.flags = NS::Game::Level::kObjectFlagGridAligned;
        object.components = NS::Game::Blocks::MaterializeLegacyKind(kind, object);
        return object;
    }
} // namespace

TEST(BlockRegistry, SlopeAndSolidObjectsAreRotatable)
{
    EXPECT_TRUE(NS::Game::Blocks::IsRotatableObject(MakeGridKind(NS::Game::Blocks::kBlockIdSlope45)));
    EXPECT_TRUE(NS::Game::Blocks::IsRotatableObject(MakeGridKind(NS::Game::Blocks::kBlockIdSlope15)));
    EXPECT_TRUE(NS::Game::Blocks::IsRotatableObject(MakeGridKind(NS::Game::Blocks::kBlockIdSolid)));
}

TEST(BlockRegistry, NonOrientableObjectsAreNotRotatable)
{
    EXPECT_FALSE(NS::Game::Blocks::IsRotatableObject(MakeGridKind(NS::Game::Blocks::kBlockIdPole)));
    EXPECT_FALSE(NS::Game::Blocks::IsRotatableObject(MakeGridKind(NS::Game::Blocks::kBlockIdWater)));
    EXPECT_FALSE(NS::Game::Blocks::IsRotatableObject(MakeGridKind(NS::Game::Blocks::kBlockIdDecoration)));
    EXPECT_FALSE(NS::Game::Blocks::IsRotatableObject(MakeGridKind(NS::Game::Blocks::kBlockIdCoin)));
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
