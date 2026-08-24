#include <Game/Entity/EntityComponent.h>
#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/CapsuleColliderComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Physics/PhysicsWorld.h>
#include <gtest/gtest.h>

#include <cmath>

namespace
{
    using NS::Core::AABB;
    using NS::Core::Vector3;
    using NS::Game::Entity::EntityComponent;
    using NS::Object::GameObject;

    constexpr float k_FixedDt = 1.0f / 60.0f;

    //! 移動と接地だけを見るための最小の派生。1 歩では呼ばれた回数を数えるだけ
    class BareEntity final : public EntityComponent
    {
    public:
        [[nodiscard]] int HandledSteps() const noexcept { return m_handledSteps; }
        [[nodiscard]] int SkippedSteps() const noexcept { return m_skippedSteps; }

    protected:
        void HandleStates(float) override { ++m_handledSteps; }
        void OnStepSkipped() override { ++m_skippedSteps; }

    private:
        int m_handledSteps = 0;
        int m_skippedSteps = 0;
    };
} // namespace

class EntityComponentTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::FrameTimer::SetFixedDelta(k_FixedDt); }
};

TEST_F(EntityComponentTest, SetVelocitySplitsLateralAndVertical)
{
    GameObject obj;
    auto& entity = *obj.AddComponent<BareEntity>();

    entity.SetVelocity(Vector3{3.0f, 0.0f, 4.0f});

    EXPECT_FLOAT_EQ(entity.LateralVelocity().x, 3.0f);
    EXPECT_FLOAT_EQ(entity.LateralVelocity().y, 0.0f);
    EXPECT_FLOAT_EQ(entity.LateralVelocity().z, 4.0f);
    EXPECT_FLOAT_EQ(entity.VerticalVelocity(), 0.0f);
}

TEST_F(EntityComponentTest, SetVerticalVelocityKeepsLateral)
{
    GameObject obj;
    auto& entity = *obj.AddComponent<BareEntity>();
    entity.SetVelocity(Vector3{3.0f, 0.0f, 4.0f});

    entity.SetVerticalVelocity(5.0f);

    EXPECT_FLOAT_EQ(entity.LateralVelocity().x, 3.0f);
    EXPECT_FLOAT_EQ(entity.LateralVelocity().z, 4.0f);
    EXPECT_FLOAT_EQ(entity.VerticalVelocity(), 5.0f);
    EXPECT_FLOAT_EQ(entity.Velocity().y, 5.0f);
}

TEST_F(EntityComponentTest, SetLateralVelocityKeepsVertical)
{
    GameObject obj;
    auto& entity = *obj.AddComponent<BareEntity>();
    entity.SetVelocity(Vector3{0.0f, 7.0f, 0.0f});

    entity.SetLateralVelocity(Vector3{1.0f, 99.0f, 2.0f});

    EXPECT_FLOAT_EQ(entity.Velocity().x, 1.0f);
    EXPECT_FLOAT_EQ(entity.Velocity().z, 2.0f);
    EXPECT_FLOAT_EQ(entity.VerticalVelocity(), 7.0f);
}

// 0 や負の寸法では掃引が成立しない。入口で下限へ丸める
TEST_F(EntityComponentTest, CapsuleSizeClampsToMinimum)
{
    GameObject obj;
    auto& entity = *obj.AddComponent<BareEntity>();

    entity.SetCapsuleRadius(0.0f);
    entity.SetCapsuleHalfHeight(-1.0f);

    EXPECT_FLOAT_EQ(entity.CapsuleRadius(), 0.001f);
    EXPECT_FLOAT_EQ(entity.CapsuleHalfHeight(), 0.001f);
}

// 当たりの形の正は同居する CapsuleColliderComponent
TEST_F(EntityComponentTest, AdoptsSiblingCapsuleColliderSize)
{
    GameObject obj;
    obj.AddComponent<NS::Object::CapsuleColliderComponent>(0.7f, 0.9f);
    auto& entity = *obj.AddComponent<BareEntity>();

    entity.OnStart();
    entity.OnUpdate();

    EXPECT_FLOAT_EQ(entity.CapsuleRadius(), 0.7f);
    EXPECT_FLOAT_EQ(entity.CapsuleHalfHeight(), 0.9f);
}

TEST_F(EntityComponentTest, UpdateStepsHandleStatesOncePerCall)
{
    GameObject obj;
    auto& entity = *obj.AddComponent<BareEntity>();

    entity.OnUpdate();
    entity.OnUpdate();

    EXPECT_EQ(entity.HandledSteps(), 2);
    EXPECT_EQ(entity.SkippedSteps(), 0);
}

// 稼働していない歩でも、1 歩限りの入力を落とす口だけは呼ぶ
TEST_F(EntityComponentTest, InactiveUpdateSkipsStatesButNotifies)
{
    GameObject obj;
    auto& entity = *obj.AddComponent<BareEntity>();
    entity.SetActive(false);

    entity.OnUpdate();

    EXPECT_EQ(entity.HandledSteps(), 0);
    EXPECT_EQ(entity.SkippedSteps(), 1);
}

// シーン無しの検証台には衝突 query 元が無い。落ちずに等速で進む
TEST_F(EntityComponentTest, MoveWithoutPhysicsWorldAdvancesByVelocity)
{
    GameObject obj;
    auto& entity = *obj.AddComponent<BareEntity>();
    obj.Root().SetPosition(Vector3{0.0f, 5.0f, 0.0f});
    entity.SetVelocity(Vector3{2.0f, 0.0f, -3.0f});

    entity.Move(k_FixedDt);

    const Vector3 pos = obj.Root().Position();
    EXPECT_NEAR(pos.x, 2.0f * k_FixedDt, 1e-5f);
    EXPECT_NEAR(pos.y, 5.0f, 1e-5f);
    EXPECT_NEAR(pos.z, -3.0f * k_FixedDt, 1e-5f);
    EXPECT_FALSE(entity.IsGrounded());
}

TEST_F(EntityComponentTest, AccelerateMatchesFirstOrderLag)
{
    GameObject obj;
    auto& entity = *obj.AddComponent<BareEntity>();

    entity.Accelerate(Vector3{4.0f, 0.0f, 0.0f}, 0.1f, k_FixedDt);

    const float expected = 4.0f * (1.0f - std::exp(-k_FixedDt / 0.1f));
    EXPECT_NEAR(entity.Velocity().x, expected, 1e-6f);
    EXPECT_FLOAT_EQ(entity.Velocity().z, 0.0f);
}

TEST_F(EntityComponentTest, AccelerateKeepsVerticalVelocity)
{
    GameObject obj;
    auto& entity = *obj.AddComponent<BareEntity>();
    entity.SetVelocity(Vector3{0.0f, -9.0f, 0.0f});

    entity.Accelerate(Vector3{4.0f, 123.0f, 0.0f}, 0.1f, k_FixedDt);

    EXPECT_FLOAT_EQ(entity.VerticalVelocity(), -9.0f);
}

TEST_F(EntityComponentTest, AccelerateWithNonPositiveTauSnapsToTarget)
{
    GameObject obj;
    auto& entity = *obj.AddComponent<BareEntity>();

    entity.Accelerate(Vector3{4.0f, 0.0f, -2.0f}, 0.0f, k_FixedDt);

    EXPECT_FLOAT_EQ(entity.Velocity().x, 4.0f);
    EXPECT_FLOAT_EQ(entity.Velocity().z, -2.0f);
}

// 減速は目標 0 の加速と同じ式。別の式に分かれると調整値の意味が 2 つになる
TEST_F(EntityComponentTest, DecelerateEqualsAccelerateTowardZero)
{
    GameObject obj;
    auto& entity = *obj.AddComponent<BareEntity>();
    entity.SetVelocity(Vector3{6.0f, 1.0f, -8.0f});

    GameObject other;
    auto& reference = *other.AddComponent<BareEntity>();
    reference.SetVelocity(Vector3{6.0f, 1.0f, -8.0f});

    entity.Decelerate(0.1f, k_FixedDt);
    reference.Accelerate(Vector3{0.0f, 0.0f, 0.0f}, 0.1f, k_FixedDt);

    EXPECT_FLOAT_EQ(entity.Velocity().x, reference.Velocity().x);
    EXPECT_FLOAT_EQ(entity.Velocity().z, reference.Velocity().z);
    EXPECT_FLOAT_EQ(entity.VerticalVelocity(), 1.0f);
}

TEST_F(EntityComponentTest, GravityChangesOnlyVerticalVelocity)
{
    GameObject obj;
    auto& entity = *obj.AddComponent<BareEntity>();
    entity.SetVelocity(Vector3{3.0f, 0.0f, 4.0f});

    entity.Gravity(-25.0f, k_FixedDt);

    EXPECT_FLOAT_EQ(entity.Velocity().x, 3.0f);
    EXPECT_FLOAT_EQ(entity.Velocity().z, 4.0f);
    EXPECT_NEAR(entity.VerticalVelocity(), -25.0f * k_FixedDt, 1e-6f);
}

TEST_F(EntityComponentTest, PositionDeltaMatchesActualMovement)
{
    GameObject obj;
    auto& entity = *obj.AddComponent<BareEntity>();
    obj.Root().SetPosition(Vector3{1.0f, 2.0f, 3.0f});
    entity.SetVelocity(Vector3{2.0f, 0.0f, -3.0f});

    const Vector3 before = obj.Root().Position();
    entity.Move(k_FixedDt);
    const Vector3 after = obj.Root().Position();

    EXPECT_FLOAT_EQ(entity.PositionDelta().x, after.x - before.x);
    EXPECT_FLOAT_EQ(entity.PositionDelta().y, after.y - before.y);
    EXPECT_FLOAT_EQ(entity.PositionDelta().z, after.z - before.z);
}

// 突進の進み具合は狙いの速度でなく実移動で測る。壁に押し付けられた歩は両者が食い違う
TEST_F(EntityComponentTest, PositionDeltaIsNearZeroWhenBlockedByWall)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    world.AddAABB(AABB{Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.5f, 4.0f, 4.0f}});
    world.BuildBroadphase();

    auto& entity = *obj.AddComponent<BareEntity>();
    entity.SetPhysicsWorld(&world);
    obj.Root().SetPosition(Vector3{0.0f, 0.0f, 0.0f});
    entity.SetVelocity(Vector3{50.0f, 0.0f, 0.0f});

    const Vector3 before = obj.Root().Position();
    entity.Move(k_FixedDt);
    const Vector3 after = obj.Root().Position();

    EXPECT_FLOAT_EQ(entity.PositionDelta().x, after.x - before.x);
    EXPECT_LT(entity.PositionDelta().x, 0.2f);
}
