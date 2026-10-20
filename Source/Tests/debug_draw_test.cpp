#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/Render/DebugDraw.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/Window.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

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

// 線の GPU の資源は描く側が装置ごとに持つ。装置を作り直しても、前の装置の資源で描かない

namespace
{
    // 縦横を奇数にして、中央のピクセルの中心を画面の中心に置く。x 軸に沿う線が中央の行を通る
    constexpr int k_ViewSize = 17;

    // 隠した窓と装置と描く段を作り、画面の中央の行を通る赤い線を描いて、
    // 中央のピクセルを青・緑・赤・不透明度の順で返す。読めなければ空
    std::vector<NS::uint8> DrawLineOnNewDeviceAndReadCenter()
    {
        NS::OS::Window window(
            NS::OS::WindowDesc{.title = "開発用の線の試し", .size = {k_ViewSize, k_ViewSize}, .visible = false});
        NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
        if (!device.IsValid())
        {
            ADD_FAILURE() << "装置を作れない";
            return {};
        }
        NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};

        std::vector<NS::uint8> pixels;
        NS::Size2D size{};
        {
            NS::Gfx::DebugShapePass pass(assets.LoadBuiltinShaders());
            NS::Gfx::DebugShapes shapes;
            // ビュー射影を単位行列にして、座標をそのまま画面の座標として描く
            shapes.Line(Vector3{-1.0f, 0.0f, 0.5f}, Vector3{1.0f, 0.0f, 0.5f}, Color{1.0f, 0.0f, 0.0f, 1.0f});
            device.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
            pass.Draw(device, shapes, NS::Matrix::Identity);
            if (!device.ReadBackbufferPixels(pixels, size))
            {
                pixels.clear();
            }
        }
        if (pixels.empty())
        {
            return {};
        }
        const std::size_t index =
            (static_cast<std::size_t>(size.height / 2) * static_cast<std::size_t>(size.width) +
             static_cast<std::size_t>(size.width / 2)) *
            4;
        return {pixels[index], pixels[index + 1], pixels[index + 2], pixels[index + 3]};
    }

    bool IsRed(const std::vector<NS::uint8>& bgra)
    {
        return bgra.size() == 4u && bgra[0] == 0 && bgra[1] == 0 && bgra[2] == 255;
    }
} // namespace

TEST(DebugShapePassTest, LineReachesTheTargetAfterTheDeviceIsRebuilt)
{
    const std::vector<NS::uint8> first = DrawLineOnNewDeviceAndReadCenter();
    ASSERT_EQ(first.size(), 4u);
    EXPECT_TRUE(IsRed(first)) << "1 つ目の装置: " << int{first[0]} << "," << int{first[1]} << "," << int{first[2]};

    const std::vector<NS::uint8> second = DrawLineOnNewDeviceAndReadCenter();
    ASSERT_EQ(second.size(), 4u);
    EXPECT_TRUE(IsRed(second)) << "2 つ目の装置: " << int{second[0]} << "," << int{second[1]} << ","
                               << int{second[2]};
}
