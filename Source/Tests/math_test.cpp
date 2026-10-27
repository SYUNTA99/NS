#include "NSlib/Core/Math.h"

#include <gtest/gtest.h>

#include <limits>

// Math.h の関数が、成立条件を満たさない値に正しい答えのような値を返さない事を縛る

namespace
{
    constexpr float k_NaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float k_Infinity = std::numeric_limits<float>::infinity();
} // namespace

// 非数の成分は長さの 2 乗の比べを素通りするので、向きにせずに偽を返す
TEST(TryNormalizeHorizontal, RejectsNaNComponent)
{
    NS::Vector3 direction{};
    EXPECT_FALSE(NS::TryNormalizeHorizontal(NS::Vector3{k_NaN, 0.0f, 1.0f}, direction));
}

TEST(TryNormalizeHorizontal, RejectsInfiniteComponent)
{
    NS::Vector3 direction{};
    EXPECT_FALSE(NS::TryNormalizeHorizontal(NS::Vector3{1.0f, 0.0f, k_Infinity}, direction));
}

// 成分は有限でも、長さの 2 乗が float に収まらなければ向きを出せない
TEST(TryNormalizeHorizontal, RejectsLengthSquaredOverflow)
{
    NS::Vector3 direction{};
    EXPECT_FALSE(NS::TryNormalizeHorizontal(NS::Vector3{1.0e30f, 0.0f, 1.0e30f}, direction));
}
