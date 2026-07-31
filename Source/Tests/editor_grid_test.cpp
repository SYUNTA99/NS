#include "Editor/GridMath.h"
#include "Runtime/Math/Math.h"

#include <cmath>
#include <gtest/gtest.h>

TEST(EditorGridTest, SnapWorldPointToGridRoundsToNearestCell)
{
    auto c = NS::Editor::SnapWorldPointToGrid({0.3f, 1.7f, -2.6f}, 1.0f);
    // 0.3 -> 0, 1.7 -> 2, -2.6 -> -3 (floor( + 0.5))
    EXPECT_FLOAT_EQ(c.x, 0.0f);
    EXPECT_FLOAT_EQ(c.y, 2.0f);
    EXPECT_FLOAT_EQ(c.z, -3.0f);
}

TEST(EditorGridTest, SnapHitToPlacementCellOffsetsByNormalPlusX)
{
    auto c = NS::Editor::SnapHitToPlacementCell({0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, 1.0f);
    // hit 0.5,0.5,0.5 → snap 1,1,1、 +X 1 grid → 2,1,1
    EXPECT_FLOAT_EQ(c.x, 2.0f);
    EXPECT_FLOAT_EQ(c.y, 1.0f);
    EXPECT_FLOAT_EQ(c.z, 1.0f);
}

TEST(EditorGridTest, SnapHitToPlacementCellOffsetsByNormalMinusY)
{
    auto c = NS::Editor::SnapHitToPlacementCell({3.2f, 4.2f, 5.8f}, {0.0f, -1.0f, 0.0f}, 1.0f);
    // hit snap → 3, 4, 6、 -Y 1 grid → 3, 3, 6
    EXPECT_FLOAT_EQ(c.x, 3.0f);
    EXPECT_FLOAT_EQ(c.y, 3.0f);
    EXPECT_FLOAT_EQ(c.z, 6.0f);
}

TEST(EditorGridTest, GroundFallbackHitsYZeroPlane)
{
    NS::Math::Ray ray{{0.3f, 5.0f, 0.7f}, {0.0f, -1.0f, 0.0f}};
    NS::Math::Vector3 out;
    ASSERT_TRUE(NS::Editor::TryGroundPlaneFallback(ray, out, 1.0f));
    EXPECT_FLOAT_EQ(out.y, 0.0f);
    EXPECT_FLOAT_EQ(out.x, 0.0f); // 0.3 → 0
    EXPECT_FLOAT_EQ(out.z, 1.0f); // 0.7 → 1
}

TEST(EditorGridTest, GroundFallbackFailsForUpwardRay)
{
    NS::Math::Ray ray{{0.0f, 5.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    NS::Math::Vector3 out{99.0f, 99.0f, 99.0f};
    EXPECT_FALSE(NS::Editor::TryGroundPlaneFallback(ray, out, 1.0f));
    // out は未変更
    EXPECT_FLOAT_EQ(out.x, 99.0f);
}

TEST(EditorGridTest, RotationU8ToQuaternionIdentityAt0)
{
    auto q = NS::Editor::RotationToQuaternion(0);
    EXPECT_NEAR(q.w, 1.0f, 1e-4f);
    EXPECT_NEAR(q.x, 0.0f, 1e-4f);
    EXPECT_NEAR(q.y, 0.0f, 1e-4f);
    EXPECT_NEAR(q.z, 0.0f, 1e-4f);
}

TEST(EditorGridTest, RotationU8At2Is180Degrees)
{
    auto q = NS::Editor::RotationToQuaternion(2);
    EXPECT_NEAR(q.w, 0.0f, 1e-4f);
    EXPECT_NEAR(std::abs(q.y), 1.0f, 1e-4f);
}

TEST(EditorGridTest, RotationU8WrapsAtModulo4)
{
    auto q4 = NS::Editor::RotationToQuaternion(4);
    auto q0 = NS::Editor::RotationToQuaternion(0);
    EXPECT_NEAR(q4.w, q0.w, 1e-4f);
}

TEST(EditorGridTest, ScreenToWorldRayDirectionIsUnitLength)
{
    NS::Math::Matrix identity = NS::Math::Matrix::Identity;
    NS::Math::Size2D vp{1280, 720};
    auto ray = NS::Editor::ScreenToWorldRay(identity, vp, 640, 360);
    const float len = std::sqrt(ray.direction.x * ray.direction.x + ray.direction.y * ray.direction.y +
                                ray.direction.z * ray.direction.z);
    EXPECT_NEAR(len, 1.0f, 1e-3f);
}

TEST(EditorGridTest, ViewRectContainsInteriorAndTopLeftEdge)
{
    const NS::Editor::ViewRect rect{100, 50, 800, 600};
    EXPECT_TRUE(NS::Editor::ViewRectContains(rect, 500, 350)); // 内点
    EXPECT_TRUE(NS::Editor::ViewRectContains(rect, 100, 50));  // 左上端は内側
}

TEST(EditorGridTest, ViewRectContainsExcludesRightBottomEdgeAndOutside)
{
    const NS::Editor::ViewRect rect{100, 50, 800, 600};
    EXPECT_FALSE(NS::Editor::ViewRectContains(rect, 900, 350)); // 右端 (x+width) は外側
    EXPECT_FALSE(NS::Editor::ViewRectContains(rect, 500, 650)); // 下端 (y+height) は外側
    EXPECT_FALSE(NS::Editor::ViewRectContains(rect, -1, -1));   // 負座標
}

TEST(EditorGridTest, ViewRectToLocalSubtractsOrigin)
{
    const NS::Editor::ViewRect rect{100, 50, 800, 600};
    int lx = 0;
    int ly = 0;
    NS::Editor::ViewRectToLocal(rect, 500, 350, lx, ly);
    EXPECT_EQ(lx, 400);
    EXPECT_EQ(ly, 300);
}

TEST(EditorGridTest, ViewRectSizeReturnsWidthHeight)
{
    const NS::Editor::ViewRect rect{100, 50, 800, 600};
    EXPECT_EQ(NS::Editor::ViewRectSize(rect), (NS::Math::Size2D{800, 600}));
}

// パネル基準のローカル化を通した ray が、原点置きの同サイズ viewport と同じ方向を返すこと
TEST(EditorGridTest, LocalizedRayMatchesOriginViewportRay)
{
    const NS::Editor::ViewRect rect{100, 50, 800, 600};
    int lx = 0;
    int ly = 0;
    NS::Editor::ViewRectToLocal(rect, 500, 350, lx, ly);

    NS::Math::Matrix identity = NS::Math::Matrix::Identity;
    const auto localized = NS::Editor::ScreenToWorldRay(identity, NS::Editor::ViewRectSize(rect), lx, ly);
    const auto reference = NS::Editor::ScreenToWorldRay(identity, NS::Math::Size2D{800, 600}, 400, 300);

    EXPECT_NEAR(localized.direction.x, reference.direction.x, 1e-5f);
    EXPECT_NEAR(localized.direction.y, reference.direction.y, 1e-5f);
    EXPECT_NEAR(localized.direction.z, reference.direction.z, 1e-5f);
}
