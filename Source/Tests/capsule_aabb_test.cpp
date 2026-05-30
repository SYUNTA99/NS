#include <gtest/gtest.h>

#include <Framework/Physics/Capsule.h>

namespace
{
    // 中心 (cx,cy,cz)、 半サイズ 0.5 の 1 cell AABB を作る。
    NS::Core::AABB MakeCellAabb(float cx, float cy, float cz)
    {
        return NS::Core::AABB{DirectX::XMFLOAT3{cx, cy, cz}, DirectX::XMFLOAT3{0.5f, 0.5f, 0.5f}};
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
        NS::Physics::IntersectsCapsuleAabb(MakePlayerCapsule(0.0f, 0.0f, 0.0f), MakeCellAabb(0.0f, 0.0f, 0.0f)));
}

// solid 衝突は capsule 中心を box 表面から radius ぶん外に押し出す。 その状態 (中心は box 外) でも
// capsule の側面は box に触れているので overlap=true でなければならない。 これがハザード未発動バグの本体。
TEST(CapsuleAabbTest, CenterOutsideButTouchingFromSideOverlaps)
{
    // box は x∈[-0.5,0.5]。 中心を x=0.85 に置く (表面 0.5 から 0.35 外 < radius 0.4)。
    EXPECT_TRUE(
        NS::Physics::IntersectsCapsuleAabb(MakePlayerCapsule(0.85f, 0.0f, 0.0f), MakeCellAabb(0.0f, 0.0f, 0.0f)));
}

// 上に乗っている状態: capsule 芯の下端が box 天面付近に来る。 中心は box の遥か上だが芯線分が接触。
TEST(CapsuleAabbTest, StandingOnTopOverlaps)
{
    EXPECT_TRUE(
        NS::Physics::IntersectsCapsuleAabb(MakePlayerCapsule(0.0f, 1.35f, 0.0f), MakeCellAabb(0.0f, 0.0f, 0.0f)));
}

TEST(CapsuleAabbTest, FarAwayDoesNotOverlap)
{
    EXPECT_FALSE(
        NS::Physics::IntersectsCapsuleAabb(MakePlayerCapsule(2.0f, 0.0f, 0.0f), MakeCellAabb(0.0f, 0.0f, 0.0f)));
}

// 表面から radius を超えて離れていれば触れていない (X 方向の境界の外側)。
TEST(CapsuleAabbTest, JustBeyondRadiusDoesNotOverlap)
{
    // 中心 x=1.0 → 表面 0.5 から 0.5 外 > radius 0.4 → 非接触。
    EXPECT_FALSE(
        NS::Physics::IntersectsCapsuleAabb(MakePlayerCapsule(1.0f, 0.0f, 0.0f), MakeCellAabb(0.0f, 0.0f, 0.0f)));
}
