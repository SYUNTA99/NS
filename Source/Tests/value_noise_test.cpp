#include "NSlib/Core/Math.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

// 揺れのずれを引くノイズ。種と位置だけで値が決まり、隣り合う位置で値が跳ばない

TEST(ValueNoise, SameSeedAndPositionGiveTheSameValue)
{
    for (int i = -20; i < 20; ++i)
    {
        const float x = static_cast<float>(i) * 0.37f;
        EXPECT_EQ(NS::ValueNoise1D(x, 7u), NS::ValueNoise1D(x, 7u));
    }
}

TEST(ValueNoise, StaysWithinOneAndSpreadsAcrossTheRange)
{
    float lowest = 1.0f;
    float highest = -1.0f;
    for (int i = -400; i < 400; ++i)
    {
        const float value = NS::ValueNoise1D(static_cast<float>(i) * 0.25f, 11u);
        EXPECT_GE(value, -1.0f);
        EXPECT_LE(value, 1.0f);
        lowest = std::fmin(lowest, value);
        highest = std::fmax(highest, value);
    }
    // 格子の値が片側に寄っていれば、揺れが片側へずれたままになる
    EXPECT_LT(lowest, -0.5f);
    EXPECT_GT(highest, 0.5f);
}

TEST(ValueNoise, NeighbouringPositionsDoNotJump)
{
    // 格子の値の差は最大 2、SmoothStep の傾きは最大 1.5 なので、位置を d 動かした時の差は 3d 以内
    constexpr float k_Step = 0.01f;
    for (int i = -500; i < 500; ++i)
    {
        const float x = static_cast<float>(i) * k_Step;
        const float difference = std::fabs(NS::ValueNoise1D(x + k_Step, 3u) - NS::ValueNoise1D(x, 3u));
        EXPECT_LE(difference, 3.0f * k_Step + 1.0e-5f);
    }
}

TEST(ValueNoise, DifferentSeedsGiveDifferentSequences)
{
    int differing = 0;
    for (int i = 0; i < 32; ++i)
    {
        const float x = static_cast<float>(i) + 0.5f;
        if (NS::ValueNoise1D(x, 1u) != NS::ValueNoise1D(x, 2u))
        {
            ++differing;
        }
    }
    EXPECT_GT(differing, 24);
}
