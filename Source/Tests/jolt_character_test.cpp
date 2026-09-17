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
    // 自機の調整値と同じ。本番と違う境目を試さない
    constexpr float k_MaxStepHeight = 0.25f;
    // 角を滑って越えられる高さは半径で変わる。段の試しは同梱シーンの自機の寸法で見る
    constexpr float k_PlayerRadius = 0.65f;
    constexpr float k_StepFrontZ = 3.0f;

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
        character.Step(position, velocity, k_Dt, k_MaxStepHeight);
    }

    struct StepRun
    {
        float finalZ = 0.0f;
        float finalY = 0.0f;
        float highestY = -1000.0f;
        float lowestYBeforeFirstFall = 1000.0f;
        int airborneFrames = 0;
    };

    // 毎フレーム前進速度を書き戻し、上昇と下降で重力を変える
    // 段で化けた縦の速度は次のフレームへ持ち越される
    StepRun RunForward(PhysicsScene& physics, Vector3 position, float speed, int frames)
    {
        physics.OptimizeBroadPhase();
        JoltCharacter character{physics, k_PlayerRadius, k_HalfHeight};
        Vector3 velocity{0.0f, 0.0f, speed};
        StepRun run;
        for (int i = 0; i < frames; ++i)
        {
            if (velocity.y > 0.0f)
            {
                velocity.y += -25.0f * k_Dt;
            }
            else
            {
                velocity.y += -35.0f * k_Dt;
            }
            velocity.z = speed;
            StepCharacter(character, position, velocity);
            position = character.Position();
            velocity = character.Velocity();
            run.highestY = std::max(run.highestY, position.y);
            if (!character.IsGrounded())
            {
                ++run.airborneFrames;
            }
            else if (run.airborneFrames == 0)
            {
                run.lowestYBeforeFirstFall = std::min(run.lowestYBeforeFirstFall, position.y);
            }
        }
        run.finalZ = position.z;
        run.finalY = position.y;
        return run;
    }

    void AddFloorWithStep(PhysicsScene& physics, float stepHeight)
    {
        physics.AddBox(MakeBox(Vector3{0.0f, -0.5f, 20.0f}, 5.0f, 0.5f, 25.0f), ObjectLayers::Terrain);
        physics.AddBox(MakeBox(Vector3{0.0f, stepHeight * 0.5f, k_StepFrontZ + 18.5f}, 5.0f, stepHeight * 0.5f, 18.5f),
                       ObjectLayers::Terrain);
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

// 壁へ押し付けたまま速度が残ると、離したフレームに溜まった勢いで急に飛び出す
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

    // 歩き速度と突進速度。1 フレームの進みが継ぎ目の手前へ落ちるかどうかで当たり方が変わる
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

// 越えられるかが速さで変わると、突進で狙った所へ当たらない。歩き・走り・突進の 3 つで見る
TEST(JoltCharacterTest, ClimbsStepsUpToMaxStepHeightWithoutBouncing)
{
    const float restOnFloor = k_PlayerRadius + k_HalfHeight;
    for (const float stepHeight : {0.05f, 0.19f, k_MaxStepHeight})
    {
        for (const float speed : {4.0f, 8.0f, 20.0f})
        {
            PhysicsScene physics;
            AddFloorWithStep(physics, stepHeight);
            const StepRun run = RunForward(physics, Vector3{0.0f, restOnFloor, 0.0f}, speed, 90);

            const float restOnStep = restOnFloor + stepHeight;
            EXPECT_GT(run.finalZ, k_StepFrontZ) << "段 " << stepHeight << " 速さ " << speed << " で止まった";
            EXPECT_NEAR(run.finalY, restOnStep, 0.01f) << "段 " << stepHeight << " 速さ " << speed;
            EXPECT_LT(run.highestY, restOnStep + 0.01f) << "段 " << stepHeight << " 速さ " << speed << " で跳ねた";
            EXPECT_EQ(run.airborneFrames, 0) << "段 " << stepHeight << " 速さ " << speed;
        }
    }
}

// 登れない高さの段を速さで越えると、ジャンプと縁つかみの出番が消える
TEST(JoltCharacterTest, StopsAtStepsAboveMaxStepHeight)
{
    const float restOnFloor = k_PlayerRadius + k_HalfHeight;
    for (const float stepHeight : {k_MaxStepHeight + 0.02f, 0.45f})
    {
        for (const float speed : {4.0f, 8.0f, 20.0f})
        {
            PhysicsScene physics;
            AddFloorWithStep(physics, stepHeight);
            const StepRun run = RunForward(physics, Vector3{0.0f, restOnFloor, 0.0f}, speed, 90);

            EXPECT_LT(run.finalZ, k_StepFrontZ) << "段 " << stepHeight << " 速さ " << speed << " を越えた";
            EXPECT_LT(run.highestY, restOnFloor + 0.01f) << "段 " << stepHeight << " 速さ " << speed << " で跳ねた";
            EXPECT_EQ(run.airborneFrames, 0) << "段 " << stepHeight << " 速さ " << speed;
        }
    }
}

// 足場の端で角に沿って沈むと、端から跳ぶ高さが下がる
// Jolt だけでも角を数 mm 転がってから離れる。吸い付けて沈んだ時は 20 cm 下がったので 1 cm で分ける
// 下の床は登れる高さより近くに置き、足元より低い床へ吸い付かないことも見る
TEST(JoltCharacterTest, DoesNotSinkAlongTheEdgeWhenRunningOffALedge)
{
    constexpr float k_LedgeTop = 2.0f;
    constexpr float k_LedgeFrontZ = 3.0f;
    const float restOnLedge = k_LedgeTop + k_PlayerRadius + k_HalfHeight;
    for (const float speed : {4.0f, 8.0f, 20.0f})
    {
        PhysicsScene physics;
        physics.AddBox(MakeBox(Vector3{0.0f, k_LedgeTop - 0.5f, k_LedgeFrontZ - 10.0f}, 5.0f, 0.5f, 10.0f),
                       ObjectLayers::Terrain);
        const float lowerTop = k_LedgeTop - 0.2f;
        physics.AddBox(MakeBox(Vector3{0.0f, lowerTop - 0.5f, k_LedgeFrontZ + 5.0f}, 5.0f, 0.5f, 5.0f),
                       ObjectLayers::Terrain);
        const StepRun run = RunForward(physics, Vector3{0.0f, restOnLedge, 0.0f}, speed, 90);

        EXPECT_GT(run.finalZ, k_LedgeFrontZ + k_PlayerRadius) << "速さ " << speed << " で端まで届いていない";
        EXPECT_GT(run.lowestYBeforeFirstFall, restOnLedge - 0.01f) << "速さ " << speed;
    }
}

// ゴールやハザードの判定の箱は sensor。床を探すレイが箱の中から始まると内側に当たり、足元の床が見えなくなる
TEST(JoltCharacterTest, RunsFlatOverTiledFloorInsideSensor)
{
    constexpr int k_TileCount = 40;
    const float restOnTiles = 0.5f + k_PlayerRadius + k_HalfHeight;
    for (const float speed : {6.0f, 20.0f})
    {
        PhysicsScene physics;
        for (int z = 0; z < k_TileCount; ++z)
        {
            physics.AddBox(MakeBox(Vector3{0.0f, 0.0f, static_cast<float>(z)}, 0.5f, 0.5f, 0.5f),
                           ObjectLayers::Terrain);
        }
        physics.AddSensorBox(MakeBox(Vector3{0.0f, 2.0f, 20.0f}, 5.0f, 3.0f, 25.0f));
        const StepRun run = RunForward(physics, Vector3{0.0f, restOnTiles, 0.0f}, speed, 90);

        EXPECT_EQ(run.airborneFrames, 0) << "速さ " << speed << " で継ぎ目から床が外れている";
        EXPECT_LT(run.highestY, restOnTiles + 0.01f) << "速さ " << speed;
    }
}

// 登れない急な角に空中で当たった時、角の斜めの法線で前進の一部が上向きの速度に変わると打ち上げられる
TEST(JoltCharacterTest, DoesNotGainHeightFromAnEdgeWhileAirborne)
{
    // 足元が 20 cm の高さ。45 cm の段の角は足元より 25 cm 上にあり、登れない急さで当たる
    const float startY = k_PlayerRadius + k_HalfHeight + 0.2f;
    // 20 cm 落ちる 0.1 秒のうちに、速さ 8 m/s でも段の角へ届く位置
    constexpr float k_StartZ = 1.8f;
    for (const float speed : {8.0f, 20.0f})
    {
        PhysicsScene physics;
        AddFloorWithStep(physics, 0.45f);
        const StepRun run = RunForward(physics, Vector3{0.0f, startY, k_StartZ}, speed, 60);

        EXPECT_LT(run.highestY, startY + 0.01f) << "速さ " << speed;
    }
}

// 接地中に上向きの速度を抜く処理と床へ吸い付ける処理が跳んだフレームにも効くと、ジャンプが出ない
TEST(JoltCharacterTest, JumpLeavesTheFloor)
{
    PhysicsScene physics;
    AddFloor(physics);
    physics.OptimizeBroadPhase();
    JoltCharacter character{physics, k_PlayerRadius, k_HalfHeight};

    const float restOnFloor = k_PlayerRadius + k_HalfHeight;
    Vector3 position{0.0f, restOnFloor, 0.0f};
    Vector3 velocity{0.0f, 0.0f, 0.0f};
    float highest = restOnFloor;
    for (int i = 0; i < 60; ++i)
    {
        velocity.y += -25.0f * k_Dt;
        if (i == 10)
        {
            velocity.y = 12.0f;
        }
        StepCharacter(character, position, velocity);
        position = character.Position();
        velocity = character.Velocity();
        highest = std::max(highest, position.y);
    }

    EXPECT_GT(highest, restOnFloor + 2.0f);
}

// 天井に押し付けたフレームに上向きの速度を残さない。残ると重力で減り切るまで天井に張り付く
TEST(JoltCharacterTest, CeilingCancelsUpwardVelocity)
{
    PhysicsScene physics;
    AddFloor(physics);
    const float restOnFloor = k_PlayerRadius + k_HalfHeight;
    const float ceilingBottom = restOnFloor + k_HalfHeight + k_PlayerRadius + 0.3f;
    physics.AddBox(MakeBox(Vector3{0.0f, ceilingBottom + 0.5f, 0.0f}, 20.0f, 0.5f, 20.0f), ObjectLayers::Terrain);
    physics.OptimizeBroadPhase();
    JoltCharacter character{physics, k_PlayerRadius, k_HalfHeight};

    Vector3 position{0.0f, restOnFloor, 0.0f};
    Vector3 velocity{0.0f, 12.0f, 0.0f};
    int framesPressedIntoCeiling = 0;
    for (int i = 0; i < 30; ++i)
    {
        velocity.y += -25.0f * k_Dt;
        StepCharacter(character, position, velocity);
        const float risen = character.Position().y - position.y;
        position = character.Position();
        velocity = character.Velocity();
        if (risen <= 0.0f && velocity.y > 0.0f)
        {
            ++framesPressedIntoCeiling;
        }
    }

    EXPECT_EQ(framesPressedIntoCeiling, 0);
}
