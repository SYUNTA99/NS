#include <gtest/gtest.h>

#include <Framework/Math/Math.h>
#include <Framework/Physics/CharacterController.h>
#include <Framework/Physics/PhysicsWorld.h>
#include <Framework/Physics/SweptTriangle.h>

#include <array>
#include <cmath>

namespace
{
    using NS::Math::Vector3;
    using NS::Physics::CharacterController;
    using NS::Physics::CharacterControllerInput;
    using NS::Physics::CharacterControllerResult;
    using NS::Physics::PhysicsWorld;
    using NS::Physics::Triangle;

    constexpr float kPi = 3.14159265358979323846f;

    /// 1×1 wedge slope の 2 三角形 (slope quad を 2 三角に分割)
    /// 底面サイズ 1×1 (X が水平、 Z が depth)、 高さは tan(angle)
    /// +Z 方向に上昇する slope (normal は (0, cos(angle), -sin(angle)) で xy=0)
    /// CCW winding は外側 (+Y, -Z 寄り) から見て反時計回り
    std::array<Triangle, 2> MakeWedgeSlope(float angleDeg) noexcept
    {
        const float h = std::tan(angleDeg * kPi / 180.0f);
        const Vector3 lowLeft{-0.5f, 0.0f, -0.5f};
        const Vector3 lowRight{0.5f, 0.0f, -0.5f};
        const Vector3 highLeft{-0.5f, h, 0.5f};
        const Vector3 highRight{0.5f, h, 0.5f};
        std::array<Triangle, 2> result;
        // 三角形 1: lowLeft → highRight → lowRight (外から見て反時計回り)
        result[0] = Triangle{lowLeft, highRight, lowRight};
        // 三角形 2: lowLeft → highLeft → highRight (外から見て反時計回り)
        result[1] = Triangle{lowLeft, highLeft, highRight};
        return result;
    }

    /// wedge slope 三角群を積んで broadphase まで作った physics world を返す
    PhysicsWorld MakeSlopeWorld(float angleDeg)
    {
        PhysicsWorld world;
        for (const Triangle& tri : MakeWedgeSlope(angleDeg))
            world.AddTriangle(tri);
        world.BuildBroadphase();
        return world;
    }
} // namespace

TEST(SlopeCollisionTest, CapsuleOnRampBecomesGrounded)
{
    CharacterController cc;
    PhysicsWorld world = MakeSlopeWorld(45.0f);

    CharacterControllerInput input;
    // capsule center を slope の中央上空 1m に置く。 重力相当の下方向 velocity で 1 step 落下させる
    input.position = Vector3{0.0f, 1.5f, 0.0f};
    input.velocity = Vector3{0.0f, -6.0f, 0.0f};
    input.dt = 0.1f;
    input.physicsWorld = &world;

    const CharacterControllerResult r = cc.Update(input);
    EXPECT_TRUE(r.grounded);
    EXPECT_GT(r.contactNormal.y, 0.7f);
}

TEST(SlopeCollisionTest, CapsuleWalkingOnRampDoesNotJitter)
{
    CharacterController cc;
    // 30 度の緩い slope を選び、 60 frame ぶん前進させても y が単調増加することを確認する
    // 急角度だと sphere 近似で contact が外れて y が振動する
    PhysicsWorld world = MakeSlopeWorld(30.0f);

    Vector3 position{0.0f, 1.0f, -0.4f};
    Vector3 velocity{0.0f, 0.0f, 0.0f};
    const float dt = 1.0f / 60.0f;

    // 接地するまで gravity だけ加えて 30 frame 沈める
    for (int i = 0; i < 30; ++i)
    {
        velocity.y -= 25.0f * dt;
        CharacterControllerInput in;
        in.position = position;
        in.velocity = velocity;
        in.dt = dt;
        in.physicsWorld = &world;
        const CharacterControllerResult r = cc.Update(in);
        position = r.position;
        velocity = r.velocity;
    }

    // 接地後、 +Z 方向に走らせて 60 frame で y が後退しないことを確認する
    float prevY = position.y;
    int monotonicCount = 0;
    for (int i = 0; i < 60; ++i)
    {
        velocity.x = 0.0f;
        velocity.z = 2.0f;
        velocity.y -= 25.0f * dt;
        CharacterControllerInput in;
        in.position = position;
        in.velocity = velocity;
        in.dt = dt;
        in.physicsWorld = &world;
        const CharacterControllerResult r = cc.Update(in);
        position = r.position;
        velocity = r.velocity;
        // 接地中の slope 上では y が単調増加 (+Z 方向上昇) するはず。 0.001m の許容で
        // 数値誤差由来の微小逆流は許す
        if (position.y >= prevY - 0.001f)
            ++monotonicCount;
        prevY = position.y;
    }
    // 60 frame 中 55 以上で単調増加が成立すれば jitter なしとみなす
    EXPECT_GE(monotonicCount, 55);
}
