#include "Runtime/Core/Math.h"
#include "Runtime/Graphics/DebugDraw.h"

#include <gtest/gtest.h>

#include <cstddef>

// 半透明の面の口: 線と別に積む・上限で古い物から捨てる・消す

namespace
{
    using NS::Core::Color;
    using NS::Core::Vector3;
    namespace DD = NS::Gfx::DebugDraw;

    // 面の上限。エディタの面を描くのに使う枚数より十分大きい
    constexpr std::size_t k_MaxFaceTriangles = 12960;

    const Color k_FaceColor{1.0f, 0.0f, 0.0f, 0.2f};

    void PushTriangle(int index)
    {
        const float x = static_cast<float>(index);
        DD::Triangle(Vector3{x, 0.0f, 0.0f}, Vector3{x + 1.0f, 0.0f, 0.0f}, Vector3{x, 1.0f, 0.0f}, k_FaceColor);
    }
} // namespace

// 面は線と別に積む。線の数は変えない
TEST(DebugDrawTest, TriangleAddsThreeFaceVerticesApartFromLines)
{
    DD::Clear();
    PushTriangle(0);
    EXPECT_EQ(DD::FaceVertexCount(), std::size_t{3});
    EXPECT_EQ(DD::VertexCount(), std::size_t{0});
}

TEST(DebugDrawTest, ClearDropsFacesToo)
{
    DD::Clear();
    PushTriangle(0);
    DD::Clear();
    EXPECT_EQ(DD::FaceVertexCount(), std::size_t{0});
}

// 面も線と同じく、前の固定ステップの分を捨てる
TEST(DebugDrawTest, BeginStepDropsTheFacesOfThePreviousStep)
{
    DD::Clear();
    PushTriangle(0);
    DD::BeginStep();
    EXPECT_EQ(DD::FaceVertexCount(), std::size_t{0});
}

// 上限の枚数までは 1 枚も捨てない
TEST(DebugDrawTest, FacesHoldUpToTheirCapacity)
{
    DD::Clear();
    for (std::size_t i = 0; i < k_MaxFaceTriangles; ++i)
    {
        PushTriangle(static_cast<int>(i));
    }
    EXPECT_EQ(DD::FaceVertexCount(), k_MaxFaceTriangles * 3);
    DD::Clear();
}

// 上限を超えたら古い物から 1 枚ずつ捨て、上限の枚数で止まる
TEST(DebugDrawTest, FaceOverflowDropsTheOldestAndStaysAtTheCapacity)
{
    DD::Clear();
    for (std::size_t i = 0; i < k_MaxFaceTriangles + 5; ++i)
    {
        PushTriangle(static_cast<int>(i));
    }
    EXPECT_EQ(DD::FaceVertexCount(), k_MaxFaceTriangles * 3);
    DD::Clear();
}
