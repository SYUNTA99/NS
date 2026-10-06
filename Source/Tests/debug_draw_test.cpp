#include "NSlib/Core/Math.h"
#include "NSlib/Graphics/DebugDraw.h"

#include <gtest/gtest.h>

#include <cstddef>

// 半透明の面の口: 線と別に積む・上限で古い物から捨てる・消す

namespace
{
    using NS::Color;
    using NS::Vector3;
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

// 図形の入れ物 DebugShapes: ステップの図形 (自由関数の積み先) とは別の溜め場で、互いに数を動かさない

namespace
{
    // 円は 12 分割。線分 1 本が頂点 2 つ
    constexpr std::size_t k_CircleVertices = 12 * 2;
    // 線の上限頂点数。ステップの図形と同じ GPU の頂点バッファを共有するので揃える
    constexpr std::size_t k_MaxLineVertices = 4096;

    const Color k_LineColor{0.0f, 1.0f, 0.0f, 1.0f};
} // namespace

TEST(DebugShapesTest, PushingIntoShapesDoesNotChangeTheStepShapes)
{
    DD::Clear();
    NS::Gfx::DebugShapes shapes;
    shapes.Line(Vector3{0.0f, 0.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_LineColor);
    shapes.Triangle(Vector3{0.0f, 0.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f}, k_FaceColor);
    EXPECT_EQ(shapes.VertexCount(), std::size_t{2});
    EXPECT_EQ(shapes.FaceVertexCount(), std::size_t{3});
    EXPECT_EQ(DD::VertexCount(), std::size_t{0});
    EXPECT_EQ(DD::FaceVertexCount(), std::size_t{0});
}

TEST(DebugShapesTest, StepShapesDoNotLandInAnIndependentShapes)
{
    DD::Clear();
    const NS::Gfx::DebugShapes shapes;
    DD::Line(Vector3{0.0f, 0.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, k_LineColor);
    EXPECT_EQ(shapes.VertexCount(), std::size_t{0});
    EXPECT_EQ(DD::VertexCount(), std::size_t{2});
    DD::Clear();
}

// 自由関数と同じ頂点の数で積む。移した形の見張り
TEST(DebugShapesTest, ShapesPushTheSameVertexCountsAsTheFreeFunctions)
{
    DD::Clear();
    NS::Gfx::DebugShapes shapes;

    shapes.AABB(NS::AABB{}, k_LineColor);
    EXPECT_EQ(shapes.VertexCount(), std::size_t{24});
    shapes.Clear();

    shapes.OBB(NS::OBB{}, k_LineColor);
    EXPECT_EQ(shapes.VertexCount(), std::size_t{24});
    shapes.Clear();

    shapes.Circle(Vector3{}, Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f}, k_LineColor);
    EXPECT_EQ(shapes.VertexCount(), k_CircleVertices);
    shapes.Clear();

    shapes.Sphere(NS::Sphere{Vector3{}, 1.0f}, k_LineColor);
    EXPECT_EQ(shapes.VertexCount(), k_CircleVertices * 3);
    shapes.Clear();

    shapes.Capsule(Vector3{}, Vector3{0.0f, 1.0f, 0.0f}, 0.5f, k_LineColor);
    EXPECT_EQ(shapes.VertexCount(), k_CircleVertices * 2 + 8);

    DD::AABB(NS::AABB{}, k_LineColor);
    EXPECT_EQ(DD::VertexCount(), std::size_t{24});
    DD::Clear();
}

TEST(DebugShapesTest, ClearDropsLinesAndFaces)
{
    NS::Gfx::DebugShapes shapes;
    shapes.Line(Vector3{}, Vector3{1.0f, 0.0f, 0.0f}, k_LineColor);
    shapes.Triangle(Vector3{}, Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f}, k_FaceColor);
    shapes.Clear();
    EXPECT_EQ(shapes.VertexCount(), std::size_t{0});
    EXPECT_EQ(shapes.FaceVertexCount(), std::size_t{0});
}

// 線も面も上限を超えたら古い物から捨て、上限で止まる
TEST(DebugShapesTest, ShapesStayAtTheirCapacityByDroppingTheOldest)
{
    NS::Gfx::DebugShapes shapes;
    for (std::size_t i = 0; i < k_MaxLineVertices; ++i)
    {
        shapes.Line(Vector3{}, Vector3{1.0f, 0.0f, 0.0f}, k_LineColor);
    }
    EXPECT_EQ(shapes.VertexCount(), k_MaxLineVertices);
    for (std::size_t i = 0; i < k_MaxFaceTriangles + 5; ++i)
    {
        shapes.Triangle(Vector3{}, Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f}, k_FaceColor);
    }
    EXPECT_EQ(shapes.FaceVertexCount(), k_MaxFaceTriangles * 3);
    EXPECT_EQ(shapes.VertexCount(), k_MaxLineVertices);
}
