#include "GameCore/Blocks/BlockRegistry.h"
#include "GameCore/Blocks/BuildPlacedObject.h"
#include "GameCore/Level/LevelData.h"

#include <gtest/gtest.h>

TEST(BlockRegistry, GridCubeIsRotatable)
{
    // grid に置く cube は固形なので R で 90° 回せる
    EXPECT_TRUE(NS::GameCore::Blocks::IsRotatableObject(NS::GameCore::Level::MakeGridObject(0, 0, 0, 0)));
}

TEST(BlockRegistry, FreeCubeRotatableMarkerNot)
{
    // 自由配置の cube も固形 box なので R で 90° 回せる。 固形判定は flags でなく BoxCollider の有無で決まる
    NS::GameCore::Level::ObjectInstance freeCube{};
    freeCube.flags = 0;
    freeCube.components = NS::GameCore::Blocks::MakeFreeCubeComponents(freeCube);
    EXPECT_TRUE(NS::GameCore::Blocks::IsRotatableObject(freeCube));

    // component を持たない空構成の object は固形でないので回せない
    NS::GameCore::Level::ObjectInstance marker{};
    EXPECT_FALSE(NS::GameCore::Blocks::IsRotatableObject(marker));
}

TEST(BlockRegistry, RotationToYawIsQuarterTurns)
{
    // rotation 0/1/2/3 が 0/90/180/270° へ対応することを確認する
    constexpr float kPi = 3.14159265358979323846f;
    EXPECT_NEAR(NS::GameCore::Blocks::BlockRotationToYaw(0), 0.0f, 1e-4f);
    EXPECT_NEAR(NS::GameCore::Blocks::BlockRotationToYaw(1), kPi * 0.5f, 1e-4f);
    EXPECT_NEAR(NS::GameCore::Blocks::BlockRotationToYaw(2), kPi, 1e-4f);
    EXPECT_NEAR(NS::GameCore::Blocks::BlockRotationToYaw(3), kPi * 1.5f, 1e-4f);
}
