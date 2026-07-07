#include "GameCore/Blocks/LedgeEdges.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace BlocksNs = NS::GameCore::Blocks;

namespace
{
    // 軸並行の固形箱を OBB で組む。 axisX/Y/Z は既定の単位基底のまま
    NS::Physics::OBB AaBox(float cx, float cy, float cz, float ex = 0.5f, float ey = 0.5f, float ez = 0.5f)
    {
        NS::Physics::OBB obb;
        obb.center = NS::Math::Vector3{cx, cy, cz};
        obb.halfExtents = NS::Math::Vector3{ex, ey, ez};
        return obb;
    }
} // namespace

TEST(LedgeEdgesTest, SingleBoxHasFourTopEdges)
{
    const auto edges = BlocksNs::ComputeTopLedgeEdges({AaBox(0.0f, 0.0f, 0.0f)});
    ASSERT_EQ(edges.size(), 4u);
    for (const auto& e : edges)
    {
        EXPECT_FLOAT_EQ(e.a.y, 0.5f);
        EXPECT_FLOAT_EQ(e.b.y, 0.5f);
    }
}

TEST(LedgeEdgesTest, TwoInRowShareNoInteriorEdge)
{
    const auto edges = BlocksNs::ComputeTopLedgeEdges({AaBox(0.0f, 0.0f, 0.0f), AaBox(1.0f, 0.0f, 0.0f)});
    // 各箱 4 縁から共有面の 2 縁を引いた外周のみ
    EXPECT_EQ(edges.size(), 6u);
    // 共有面 x=0.5 を Z 方向に走る縁辺が無いこと
    for (const auto& e : edges)
    {
        const bool onSharedFace = (e.a.x == 0.5f && e.b.x == 0.5f);
        EXPECT_FALSE(onSharedFace);
    }
}

TEST(LedgeEdgesTest, CoveredTopHasNoEdges)
{
    // 真上に固形を載せ天面を塞ぐ
    const auto edges = BlocksNs::ComputeTopLedgeEdges({AaBox(0.0f, 0.0f, 0.0f), AaBox(0.0f, 1.0f, 0.0f)});
    // 下の箱は天面が塞がれ縁ゼロ。 上の箱だけが 4 縁を出す
    ASSERT_EQ(edges.size(), 4u);
    for (const auto& e : edges)
        EXPECT_FLOAT_EQ(e.a.y, 1.5f);
}

TEST(LedgeEdgesTest, GapBetweenBoxesKeepsBothInnerEdges)
{
    // 5cm を超える隙間があれば地続きでなく、 対向面の縁は踏み外せる縁として残る
    const auto edges = BlocksNs::ComputeTopLedgeEdges({AaBox(0.0f, 0.0f, 0.0f), AaBox(1.2f, 0.0f, 0.0f)});
    EXPECT_EQ(edges.size(), 8u);
}

TEST(LedgeEdgesTest, ScaledBoxEdgesFollowExtents)
{
    // 2 倍に拡大した箱は、 縁が実際の天面高さと広がりに追従する
    const auto edges = BlocksNs::ComputeTopLedgeEdges({AaBox(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f)});
    ASSERT_EQ(edges.size(), 4u);
    for (const auto& e : edges)
    {
        EXPECT_FLOAT_EQ(e.a.y, 1.0f);
        EXPECT_FLOAT_EQ(e.b.y, 1.0f);
    }
    bool foundPlusX = false;
    for (const auto& e : edges)
        if (e.outward.x > 0.5f)
        {
            foundPlusX = true;
            EXPECT_FLOAT_EQ(e.a.x, 1.0f);
            EXPECT_FLOAT_EQ(e.b.x, 1.0f);
        }
    EXPECT_TRUE(foundPlusX);
}

TEST(LedgeEdgesTest, OffGridBoxEdgesFollowPosition)
{
    // グリッド外へ動かした箱は、 縁が実位置に追従する
    const auto edges = BlocksNs::ComputeTopLedgeEdges({AaBox(3.3f, 0.0f, 0.0f)});
    ASSERT_EQ(edges.size(), 4u);
    bool foundPlusX = false;
    for (const auto& e : edges)
        if (e.outward.x > 0.5f)
        {
            foundPlusX = true;
            EXPECT_NEAR(e.a.x, 3.8f, 1.0e-5f);
        }
    EXPECT_TRUE(foundPlusX);
}

TEST(LedgeEdgesTest, YawRotatedBoxEdgesFollowOrientation)
{
    // yaw 45° に回した箱の天面は 45° 回った正方形。 縁の隅が軸並行の 0.5 でなく √2/2 まで張り出す
    constexpr float kQuarterPi = 0.78539816339744830961f;
    const NS::Physics::OBB obb =
        NS::Physics::MakeObb(NS::Math::Vector3{0.0f, 0.0f, 0.0f},
                             NS::Math::Quaternion::CreateFromAxisAngle(NS::Math::Vector3{0.0f, 1.0f, 0.0f}, kQuarterPi),
                             NS::Math::Vector3{0.5f, 0.5f, 0.5f});
    const auto edges = BlocksNs::ComputeTopLedgeEdges({obb});
    ASSERT_EQ(edges.size(), 4u);

    float maxAbsX = 0.0f;
    for (const auto& e : edges)
    {
        maxAbsX = std::max(maxAbsX, std::fabs(e.a.x));
        maxAbsX = std::max(maxAbsX, std::fabs(e.b.x));
        // yaw なので天面は水平のまま
        EXPECT_NEAR(e.a.y, 0.5f, 1.0e-5f);
        EXPECT_NEAR(e.b.y, 0.5f, 1.0e-5f);
    }
    EXPECT_NEAR(maxAbsX, 0.70710678f, 1.0e-4f);
}

TEST(LedgeEdgesTest, UpsideDownBoxEdgesStayOnTop)
{
    // X 軸まわり 180° で上下反転しても、 縁は実際に上を向く面 = 天面(y=+0.5) に出る
    // axisY 決め打ちだと反転で axisY が真下を向き、 縁が底面(y=-0.5) に出てしまう
    constexpr float kPi = 3.14159265358979323846f;
    const NS::Physics::OBB obb =
        NS::Physics::MakeObb(NS::Math::Vector3{0.0f, 0.0f, 0.0f},
                             NS::Math::Quaternion::CreateFromAxisAngle(NS::Math::Vector3{1.0f, 0.0f, 0.0f}, kPi),
                             NS::Math::Vector3{0.5f, 0.5f, 0.5f});
    const auto edges = BlocksNs::ComputeTopLedgeEdges({obb});
    ASSERT_EQ(edges.size(), 4u);
    for (const auto& e : edges)
    {
        EXPECT_NEAR(e.a.y, 0.5f, 1.0e-5f);
        EXPECT_NEAR(e.b.y, 0.5f, 1.0e-5f);
    }
}
