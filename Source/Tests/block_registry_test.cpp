#include "Game/Blocks/BlockRegistry.h"
#include "Game/Blocks/BuildPlacedObject.h"
#include "Game/Level/LevelData.h"

#include <gtest/gtest.h>

TEST(BlockRegistry, GridCubeIsRotatable)
{
    // grid に置く cube は固形なので R で 90° 回せる
    EXPECT_TRUE(NS::Game::Blocks::IsRotatableObject(NS::Game::Level::MakeGridObject(0, 0, 0, 0)));
}

TEST(BlockRegistry, FreeCubeAndMarkerAreNotRotatable)
{
    // 自由配置の cube は grid 固形でないため回せない
    NS::Game::Level::ObjectInstance freeCube{};
    freeCube.flags = 0;
    freeCube.components = NS::Game::Blocks::MakeFreeCubeComponents(freeCube);
    EXPECT_FALSE(NS::Game::Blocks::IsRotatableObject(freeCube));

    // component を持たない spawn marker も回せない
    NS::Game::Level::ObjectInstance marker{};
    EXPECT_FALSE(NS::Game::Blocks::IsRotatableObject(marker));
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
