#include <gtest/gtest.h>
#include <Runtime/Physics/Capsule.h>

namespace
{
    // 中心 (cx,cy,cz)、 半サイズ 0.5 の 1 cell AABB を作る
    NS::Math::AABB MakeCellAabb(float cx, float cy, float cz)
    {
        return NS::Math::AABB{DirectX::XMFLOAT3{cx, cy, cz}, DirectX::XMFLOAT3{0.5f, 0.5f, 0.5f}};
    }

    NS::Physics::Capsule MakePlayerCapsule(float x, float y, float z)
    {
        NS::Physics::Capsule cap{};
        cap.center = {x, y, z};
        cap.radius = 0.4f;
        cap.halfHeight = 0.5f;
        return cap;
    }
} // namespace

TEST(CapsuleAabbTest, CenterInsideOverlaps)
{
    EXPECT_TRUE(
        NS::Physics::IntersectsCapsuleAABB(MakePlayerCapsule(0.0f, 0.0f, 0.0f), MakeCellAabb(0.0f, 0.0f, 0.0f)));
}

// solid 押し出し後は中心が box 外でも側面は触れている。 ここを偽と返すとハザードが発動しない
TEST(CapsuleAabbTest, CenterOutsideButTouchingFromSideOverlaps)
{
    // box は x∈[-0.5,0.5]。 中心を x=0.85 に置く (表面 0.5 から 0.35 外 < radius 0.4)
    EXPECT_TRUE(
        NS::Physics::IntersectsCapsuleAABB(MakePlayerCapsule(0.85f, 0.0f, 0.0f), MakeCellAabb(0.0f, 0.0f, 0.0f)));
}

// 上に乗っている状態: capsule 軸の下端が box 天面付近に来る。 中心は box の遥か上だが軸線分が接触
TEST(CapsuleAabbTest, StandingOnTopOverlaps)
{
    EXPECT_TRUE(
        NS::Physics::IntersectsCapsuleAABB(MakePlayerCapsule(0.0f, 1.35f, 0.0f), MakeCellAabb(0.0f, 0.0f, 0.0f)));
}

TEST(CapsuleAabbTest, FarAwayDoesNotOverlap)
{
    EXPECT_FALSE(
        NS::Physics::IntersectsCapsuleAABB(MakePlayerCapsule(2.0f, 0.0f, 0.0f), MakeCellAabb(0.0f, 0.0f, 0.0f)));
}

// 表面から radius を超えて離れていれば触れていない
TEST(CapsuleAabbTest, JustBeyondRadiusDoesNotOverlap)
{
    // 中心 x=1.0 → 表面 0.5 から 0.5 外 > radius 0.4 → 非接触
    EXPECT_FALSE(
        NS::Physics::IntersectsCapsuleAABB(MakePlayerCapsule(1.0f, 0.0f, 0.0f), MakeCellAabb(0.0f, 0.0f, 0.0f)));
}
