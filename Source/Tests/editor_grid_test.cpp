#include "Framework/Core/Math.h"
#include "Framework/Scene/EditorCameraComponent.h"

#include <gtest/gtest.h>

#include <cmath>

namespace SceneNs = NS::Scene;

TEST(EditorGridTest, SnapWorldPointToGridRoundsToNearestCell)
{
    auto c = SceneNs::EditorGridMath::SnapWorldPointToGrid({0.3f, 1.7f, -2.6f}, 1.0f);
    // 0.3 -> 0, 1.7 -> 2, -2.6 -> -3 (floor( + 0.5))
    EXPECT_FLOAT_EQ(c.x, 0.0f);
    EXPECT_FLOAT_EQ(c.y, 2.0f);
    EXPECT_FLOAT_EQ(c.z, -3.0f);
}

TEST(EditorGridTest, SnapHitToPlacementCellOffsetsByNormalPlusX)
{
    auto c = SceneNs::EditorGridMath::SnapHitToPlacementCell({0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, 1.0f);
    // hit 0.5,0.5,0.5 → snap 1,1,1、 +X 1 grid → 2,1,1
    EXPECT_FLOAT_EQ(c.x, 2.0f);
    EXPECT_FLOAT_EQ(c.y, 1.0f);
    EXPECT_FLOAT_EQ(c.z, 1.0f);
}

TEST(EditorGridTest, SnapHitToPlacementCellOffsetsByNormalMinusY)
{
    auto c = SceneNs::EditorGridMath::SnapHitToPlacementCell({3.2f, 4.2f, 5.8f}, {0.0f, -1.0f, 0.0f}, 1.0f);
    // hit snap → 3, 4, 6、 -Y 1 grid → 3, 3, 6
    EXPECT_FLOAT_EQ(c.x, 3.0f);
    EXPECT_FLOAT_EQ(c.y, 3.0f);
    EXPECT_FLOAT_EQ(c.z, 6.0f);
}

TEST(EditorGridTest, GroundFallbackHitsYZeroPlane)
{
    NS::Math::Ray ray{{0.3f, 5.0f, 0.7f}, {0.0f, -1.0f, 0.0f}};
    NS::Math::Vector3 out;
    ASSERT_TRUE(SceneNs::EditorGridMath::TryGroundPlaneFallback(ray, out, 1.0f));
    EXPECT_FLOAT_EQ(out.y, 0.0f);
    EXPECT_FLOAT_EQ(out.x, 0.0f); // 0.3 → 0
    EXPECT_FLOAT_EQ(out.z, 1.0f); // 0.7 → 1
}

TEST(EditorGridTest, GroundFallbackFailsForUpwardRay)
{
    NS::Math::Ray ray{{0.0f, 5.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    NS::Math::Vector3 out{99.0f, 99.0f, 99.0f};
    EXPECT_FALSE(SceneNs::EditorGridMath::TryGroundPlaneFallback(ray, out, 1.0f));
    // out は未変更
    EXPECT_FLOAT_EQ(out.x, 99.0f);
}

TEST(EditorGridTest, RotationU8ToQuaternionIdentityAt0)
{
    auto q = SceneNs::EditorGridMath::RotationToQuaternion(0);
    EXPECT_NEAR(q.w, 1.0f, 1e-4f);
    EXPECT_NEAR(q.x, 0.0f, 1e-4f);
    EXPECT_NEAR(q.y, 0.0f, 1e-4f);
    EXPECT_NEAR(q.z, 0.0f, 1e-4f);
}

TEST(EditorGridTest, RotationU8At2Is180Degrees)
{
    auto q = SceneNs::EditorGridMath::RotationToQuaternion(2);
    EXPECT_NEAR(q.w, 0.0f, 1e-4f);
    EXPECT_NEAR(std::abs(q.y), 1.0f, 1e-4f);
}

TEST(EditorGridTest, RotationU8WrapsAtModulo4)
{
    auto q4 = SceneNs::EditorGridMath::RotationToQuaternion(4);
    auto q0 = SceneNs::EditorGridMath::RotationToQuaternion(0);
    EXPECT_NEAR(q4.w, q0.w, 1e-4f);
}

TEST(EditorGridTest, ScreenToWorldRayDirectionIsUnitLength)
{
    NS::Math::Matrix identity = NS::Math::Matrix::Identity;
    NS::Math::Size2D vp{1280, 720};
    auto ray = SceneNs::EditorGridMath::ScreenToWorldRay(identity, vp, 640, 360);
    const float len = std::sqrt(ray.direction.x * ray.direction.x + ray.direction.y * ray.direction.y +
                                ray.direction.z * ray.direction.z);
    EXPECT_NEAR(len, 1.0f, 1e-3f);
}
