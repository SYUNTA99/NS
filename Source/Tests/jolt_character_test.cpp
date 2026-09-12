#include <Runtime/Core/Math.h>
#include <Runtime/Core/OBB.h>
#include <Runtime/Physics/JoltCharacter.h>
#include <Runtime/Physics/PhysicsScene.h>

#include <algorithm>
#include <gtest/gtest.h>

namespace
{
    using NS::Core::OBB;
    using NS::Core::Vector3;
    using NS::Physics::JoltCharacter;
    using NS::Physics::PhysicsScene;
    namespace ObjectLayers = NS::Physics::ObjectLayers;

    constexpr float k_Dt = 1.0f / 60.0f;
    constexpr float k_Radius = 0.4f;
    constexpr float k_HalfHeight = 0.5f;

    OBB MakeBox(const Vector3& center, float halfX, float halfY, float halfZ)
    {
        OBB box;
        box.center = center;
        box.halfExtentX = halfX;
        box.halfExtentY = halfY;
        box.halfExtentZ = halfZ;
        return box;
    }

    void AddFloor(PhysicsScene& physics)
    {
        physics.AddBox(MakeBox(Vector3{0.0f, -0.5f, 0.0f}, 20.0f, 0.5f, 20.0f), ObjectLayers::Terrain);
    }

    void StepCharacter(JoltCharacter& character, const Vector3& position, const Vector3& velocity)
    {
        character.Step(position, velocity, k_Dt);
    }
} // namespace

TEST(JoltCharacterTest, FreeFallProgressesWhenThePhysicsSceneIsEmpty)
{
    PhysicsScene physics;
    physics.OptimizeBroadPhase();
    JoltCharacter character{physics, k_Radius, k_HalfHeight};

    StepCharacter(character, Vector3{0.0f, 5.0f, 0.0f}, Vector3{0.0f, -10.0f, 0.0f});

    EXPECT_LT(character.Position().y, 5.0f);
    EXPECT_FALSE(character.IsGrounded());
}

TEST(JoltCharacterTest, LandsOnFloorAndReportsGrounded)
{
    PhysicsScene physics;
    AddFloor(physics);
    physics.OptimizeBroadPhase();
    JoltCharacter character{physics, k_Radius, k_HalfHeight};

    Vector3 position{0.0f, 3.0f, 0.0f};
    bool grounded = false;
    for (int i = 0; i < 120 && !grounded; ++i)
    {
        StepCharacter(character, position, Vector3{0.0f, -12.0f, 0.0f});
        position = character.Position();
        grounded = character.IsGrounded();
    }

    EXPECT_TRUE(grounded);
    EXPECT_NEAR(position.y, k_HalfHeight + k_Radius, 0.05f);
}

TEST(JoltCharacterTest, NoGroundedWhenAirborne)
{
    PhysicsScene physics;
    AddFloor(physics);
    physics.OptimizeBroadPhase();
    JoltCharacter character{physics, k_Radius, k_HalfHeight};

    StepCharacter(character, Vector3{0.0f, 5.0f, 0.0f}, Vector3{0.0f, 0.0f, 0.0f});

    EXPECT_FALSE(character.IsGrounded());
}

TEST(JoltCharacterTest, StopsAtWall)
{
    PhysicsScene physics;
    AddFloor(physics);
    physics.AddBox(MakeBox(Vector3{2.0f, 1.0f, 0.0f}, 0.5f, 1.0f, 4.0f), ObjectLayers::Terrain);
    physics.OptimizeBroadPhase();
    JoltCharacter character{physics, k_Radius, k_HalfHeight};

    Vector3 position{0.0f, k_HalfHeight + k_Radius, 0.0f};
    for (int i = 0; i < 120; ++i)
    {
        StepCharacter(character, position, Vector3{8.0f, 0.0f, 0.0f});
        position = character.Position();
    }

    EXPECT_LT(position.x, 1.5f - k_Radius + 0.05f);
}

// sensor は通知だけの体積。壁と同じ場所に置いても足を止めない
TEST(JoltCharacterTest, PassesThroughSensorBox)
{
    PhysicsScene physics;
    AddFloor(physics);
    physics.AddSensorBox(MakeBox(Vector3{2.0f, 1.0f, 0.0f}, 0.5f, 1.0f, 4.0f));
    physics.OptimizeBroadPhase();
    JoltCharacter character{physics, k_Radius, k_HalfHeight};

    Vector3 position{0.0f, k_HalfHeight + k_Radius, 0.0f};
    for (int i = 0; i < 120; ++i)
    {
        StepCharacter(character, position, Vector3{8.0f, 0.0f, 0.0f});
        position = character.Position();
    }

    EXPECT_GT(position.x, 3.0f);
}

TEST(JoltCharacterTest, DeterministicUnderSameInput)
{
    PhysicsScene first;
    AddFloor(first);
    first.OptimizeBroadPhase();
    JoltCharacter firstCharacter{first, k_Radius, k_HalfHeight};

    PhysicsScene second;
    AddFloor(second);
    second.OptimizeBroadPhase();
    JoltCharacter secondCharacter{second, k_Radius, k_HalfHeight};

    Vector3 firstPosition{0.0f, 3.0f, 0.0f};
    Vector3 secondPosition{0.0f, 3.0f, 0.0f};
    for (int i = 0; i < 60; ++i)
    {
        StepCharacter(firstCharacter, firstPosition, Vector3{3.0f, -9.0f, 0.0f});
        StepCharacter(secondCharacter, secondPosition, Vector3{3.0f, -9.0f, 0.0f});
        firstPosition = firstCharacter.Position();
        secondPosition = secondCharacter.Position();
    }

    EXPECT_FLOAT_EQ(firstPosition.x, secondPosition.x);
    EXPECT_FLOAT_EQ(firstPosition.y, secondPosition.y);
    EXPECT_FLOAT_EQ(firstPosition.z, secondPosition.z);
}

TEST(JoltCharacterTest, GroundCancelsVelocityIntoTheFloor)
{
    PhysicsScene physics;
    AddFloor(physics);
    physics.OptimizeBroadPhase();
    JoltCharacter character{physics, k_Radius, k_HalfHeight};

    Vector3 position{0.0f, 3.0f, 0.0f};
    Vector3 velocity{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 240; ++i)
    {
        velocity.y += -25.0f * k_Dt;
        StepCharacter(character, position, velocity);
        position = character.Position();
        velocity = character.Velocity();
    }

    EXPECT_NEAR(velocity.y, 0.0f, 0.5f);
}

// 壁へ押し付けたまま速度が残ると、離した歩に溜まった勢いで急に飛び出す
TEST(JoltCharacterTest, WallCancelsVelocityIntoIt)
{
    PhysicsScene physics;
    AddFloor(physics);
    physics.AddBox(MakeBox(Vector3{2.0f, 1.0f, 0.0f}, 0.5f, 1.0f, 4.0f), ObjectLayers::Terrain);
    physics.OptimizeBroadPhase();
    JoltCharacter character{physics, k_Radius, k_HalfHeight};

    Vector3 position{0.0f, k_HalfHeight + k_Radius, 0.0f};
    Vector3 velocity{8.0f, 0.0f, 0.0f};
    for (int i = 0; i < 120; ++i)
    {
        StepCharacter(character, position, Vector3{8.0f, velocity.y, 0.0f});
        position = character.Position();
        velocity = character.Velocity();
    }

    EXPECT_LT(velocity.x, 0.5f);
}

// 床を 1 マスずつ並べた上を走る。継ぎ目で跳ね上がると縁つかみや落下判定が誤発火する
TEST(JoltCharacterTest, RunsFlatOverTiledFloor)
{
    constexpr float k_TileTop = 0.5f;
    constexpr float k_RestY = k_TileTop + k_HalfHeight + k_Radius;
    constexpr int k_TileCount = 40;

    // 歩き速度と突進速度。1 歩の進みが継ぎ目の手前へ落ちるかどうかで当たり方が変わる
    for (const float speed : {6.0f, 20.0f})
    {
        PhysicsScene physics;
        for (int z = 0; z < k_TileCount; ++z)
        {
            physics.AddBox(MakeBox(Vector3{0.0f, 0.0f, static_cast<float>(z)}, 0.5f, 0.5f, 0.5f), ObjectLayers::Terrain);
        }
        physics.OptimizeBroadPhase();
        JoltCharacter character{physics, k_Radius, k_HalfHeight};

        Vector3 position{0.0f, k_RestY, 0.0f};
        Vector3 velocity{0.0f, 0.0f, speed};
        float lowest = k_RestY;
        float highest = k_RestY;
        int airborneSteps = 0;
        const int steps = static_cast<int>(30.0f / (speed * k_Dt));
        for (int i = 0; i < steps; ++i)
        {
            velocity.y += -25.0f * k_Dt;
            velocity.z = speed;
            StepCharacter(character, position, velocity);
            position = character.Position();
            velocity = character.Velocity();
            lowest = std::min(lowest, position.y);
            highest = std::max(highest, position.y);
            if (!character.IsGrounded())
                ++airborneSteps;
        }

        EXPECT_EQ(airborneSteps, 0) << "速さ " << speed << " で継ぎ目から床が外れている";
        EXPECT_NEAR(lowest, k_RestY, 0.03f) << "速さ " << speed;
        EXPECT_NEAR(highest, k_RestY, 0.03f) << "速さ " << speed;
    }
}
