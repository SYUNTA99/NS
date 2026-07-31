#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <limits>
#include <Runtime/Physics/WedgeGeometry.h>
#include <vector>

namespace
{
    using NS::Math::Vector3;
    using NS::Physics::BuildWedgeTriangles;
    using NS::Physics::Triangle;

    // 8 三角形の全頂点から最高 y 付近 (斜面の高い辺) の頂点を集め、 その XZ 重心を返す
    // 三角形ごとに頂点が重複して現れ、 かつ重複回数が頂点で異なるため、 重心が偏らないよう
    // 一意な頂点だけを集計する。 wedge の「向き」 は高い辺がどちらを向くかで決まる
    Vector3 HighEdgeCentroid(const std::array<Triangle, 8>& tris)
    {
        float maxY = -std::numeric_limits<float>::infinity();
        for (const auto& t : tris)
            for (const auto* v : {&t.v0, &t.v1, &t.v2})
                maxY = std::max(v->y, maxY);

        std::vector<Vector3> unique;
        for (const auto& t : tris)
            for (const auto* v : {&t.v0, &t.v1, &t.v2})
            {
                if (std::abs(v->y - maxY) >= 1e-4f)
                    continue;
                const bool seen = std::any_of(unique.begin(), unique.end(), [&](const Vector3& u) {
                    return std::abs(u.x - v->x) < 1e-4f && std::abs(u.z - v->z) < 1e-4f;
                });
                if (!seen)
                    unique.push_back(*v);
            }

        Vector3 sum{0.0f, 0.0f, 0.0f};
        for (const auto& u : unique)
        {
            sum.x += u.x;
            sum.z += u.z;
        }
        if (!unique.empty())
        {
            sum.x /= static_cast<float>(unique.size());
            sum.z /= static_cast<float>(unique.size());
        }
        return sum;
    }
} // namespace

// 既定 (quadrant=0): 高い辺は +Z 側 (z>0、 x はほぼ 0)
namespace
{
    constexpr float k_HalfPi = 1.5707963267948966f;
    constexpr float k_QuarterPi = 0.7853981633974483f;
} // namespace

TEST(WedgeGeometryTest, DefaultFacesPositiveZ)
{
    const auto tris = BuildWedgeTriangles(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.5f, 0.5f, 0.5f}, 45.0f, 0.0f);
    const Vector3 high = HighEdgeCentroid(tris);
    EXPECT_GT(high.z, 0.4f);
    EXPECT_NEAR(high.x, 0.0f, 1e-3f);
}

// 回帰: yaw=90° で高い辺が +X 側へ回る。 rotation を無視する実装では FAIL する
TEST(WedgeGeometryTest, Rotation90FacesPositiveX)
{
    const auto tris = BuildWedgeTriangles(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.5f, 0.5f, 0.5f}, 45.0f, k_HalfPi);
    const Vector3 high = HighEdgeCentroid(tris);
    EXPECT_GT(high.x, 0.4f);
    EXPECT_NEAR(high.z, 0.0f, 1e-3f);
}

// 連続回転: yaw=45° で高い辺が +X +Z の対角を向く (90° スナップではない自由角を検証)
TEST(WedgeGeometryTest, Rotation45FacesDiagonal)
{
    const auto tris = BuildWedgeTriangles(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.5f, 0.5f, 0.5f}, 45.0f, k_QuarterPi);
    const Vector3 high = HighEdgeCentroid(tris);
    EXPECT_GT(high.x, 0.2f);
    EXPECT_GT(high.z, 0.2f);
    EXPECT_NEAR(high.x, high.z, 1e-2f);
}

// Y 軸回転なので高さ (max y) は yaw に依らず不変
TEST(WedgeGeometryTest, RotationPreservesHeight)
{
    const auto t0 = BuildWedgeTriangles(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.5f, 0.5f, 0.5f}, 45.0f, 0.0f);
    const auto t1 = BuildWedgeTriangles(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.5f, 0.5f, 0.5f}, 45.0f, k_QuarterPi);
    EXPECT_NEAR(HighEdgeCentroid(t0).y, HighEdgeCentroid(t1).y, 1e-4f);
}

// center 指定が反映される (world 配置)
TEST(WedgeGeometryTest, CenterOffsetsAllVertices)
{
    const Vector3 c{10.0f, 2.0f, -3.0f};
    const auto tris = BuildWedgeTriangles(c, Vector3{0.5f, 0.5f, 0.5f}, 45.0f, 0.0f);
    // 最低頂点 (底面) の y は center.y - 0.5
    float minY = std::numeric_limits<float>::infinity();
    for (const auto& t : tris)
        for (const auto* v : {&t.v0, &t.v1, &t.v2})
            minY = std::min(v->y, minY);
    EXPECT_NEAR(minY, c.y - 0.5f, 1e-4f);
}
