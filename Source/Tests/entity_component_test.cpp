#include <Game/Entity/EntityComponent.h>
#include <Runtime/Core/AABB.h>
#include <Runtime/Platform/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/CapsuleCollider.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Transform.h>
#include <gtest/gtest.h>

#include <cmath>

namespace
{
    using NS::Core::AABB;
    using NS::Core::Vector3;
    using NS::Game::Entity::EntityComponent;
    using NS::Obj::GameObject;

    constexpr float k_FixedDt = 1.0f / 60.0f;

    //! 移動と接地だけを見るための最小の派生。1 フレームの中身は呼ばれた回数を数えるだけ
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
    void SetUp() override { NS::Platform::FrameTimer::SetFixedDelta(k_FixedDt); }
};

TEST_F(EntityComponentTest, SetVelocitySplitsLateralAndVertical)
{
    GameObject obj;
    BareEntity& entity = *obj.AddComponent<BareEntity>();

    entity.SetVelocity(Vector3{3.0f, 0.0f, 4.0f});

    EXPECT_FLOAT_EQ(entity.LateralVelocity().x, 3.0f);
    EXPECT_FLOAT_EQ(entity.LateralVelocity().y, 0.0f);
    EXPECT_FLOAT_EQ(entity.LateralVelocity().z, 4.0f);
    EXPECT_FLOAT_EQ(entity.VerticalVelocity(), 0.0f);
}

TEST_F(EntityComponentTest, SetVerticalVelocityKeepsLateral)
{
    GameObject obj;
    BareEntity& entity = *obj.AddComponent<BareEntity>();
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
    BareEntity& entity = *obj.AddComponent<BareEntity>();
    entity.SetVelocity(Vector3{0.0f, 7.0f, 0.0f});

    entity.SetLateralVelocity(Vector3{1.0f, 99.0f, 2.0f});

    EXPECT_FLOAT_EQ(entity.Velocity().x, 1.0f);
    EXPECT_FLOAT_EQ(entity.Velocity().z, 2.0f);
    EXPECT_FLOAT_EQ(entity.VerticalVelocity(), 7.0f);
}

// collider を持たない最小 Entity は既定寸法で動く
TEST_F(EntityComponentTest, CapsuleSizeDefaultsWithoutCollider)
{
    GameObject obj;
    BareEntity& entity = *obj.AddComponent<BareEntity>();

    EXPECT_FLOAT_EQ(entity.CapsuleRadius(), 0.4f);
    EXPECT_FLOAT_EQ(entity.CapsuleHalfHeight(), 0.5f);
}

// 当たりの形の正は同居する CapsuleCollider
TEST_F(EntityComponentTest, AdoptsSiblingCapsuleColliderSize)
{
    GameObject obj;
    obj.AddComponent<NS::Obj::CapsuleCollider>(0.7f, 0.9f);
    BareEntity& entity = *obj.AddComponent<BareEntity>();

    entity.OnStart();

    EXPECT_FLOAT_EQ(entity.CapsuleRadius(), 0.7f);
    EXPECT_FLOAT_EQ(entity.CapsuleHalfHeight(), 0.9f);
}

TEST_F(EntityComponentTest, UpdateStepsHandleStatesOncePerCall)
{
    GameObject obj;
    BareEntity& entity = *obj.AddComponent<BareEntity>();

    entity.OnUpdate();
    entity.OnUpdate();

    EXPECT_EQ(entity.HandledSteps(), 2);
    EXPECT_EQ(entity.SkippedSteps(), 0);
}

TEST_F(EntityComponentTest, UpdateMovesOncePerCall)
{
    GameObject obj;
    BareEntity& entity = *obj.AddComponent<BareEntity>();
    entity.SetVelocity(Vector3{6.0f, 0.0f, 0.0f});

    entity.OnUpdate();

    EXPECT_NEAR(obj.Root().Position().x, 6.0f * k_FixedDt, 1e-5f);
}

// 稼働していないフレームでも、1 フレーム限りの入力を落とす口だけは呼ぶ
TEST_F(EntityComponentTest, InactiveUpdateSkipsStatesButNotifies)
{
    GameObject obj;
    BareEntity& entity = *obj.AddComponent<BareEntity>();
    entity.SetActive(false);

    entity.OnUpdate();

    EXPECT_EQ(entity.HandledSteps(), 0);
    EXPECT_EQ(entity.SkippedSteps(), 1);
}

// Scene に居ない GameObject からは PhysicsScene を引けない。落ちずに等速で進む
TEST_F(EntityComponentTest, MoveWithoutPhysicsSceneAdvancesByVelocity)
{
    GameObject obj;
    BareEntity& entity = *obj.AddComponent<BareEntity>();
    obj.Root().SetPosition(Vector3{0.0f, 5.0f, 0.0f});
    entity.SetVelocity(Vector3{2.0f, 0.0f, -3.0f});

    entity.Move(k_FixedDt, 0.0f);

    const Vector3 pos = obj.Root().Position();
    EXPECT_NEAR(pos.x, 2.0f * k_FixedDt, 1e-5f);
    EXPECT_NEAR(pos.y, 5.0f, 1e-5f);
    EXPECT_NEAR(pos.z, -3.0f * k_FixedDt, 1e-5f);
    EXPECT_FALSE(entity.IsGrounded());
}

TEST_F(EntityComponentTest, AccelerateAddsTheAccelerationTimesDt)
{
    GameObject obj;
    BareEntity& entity = *obj.AddComponent<BareEntity>();

    entity.Accelerate(Vector3{1.0f, 0.0f, 0.0f}, 0.0f, 60.0f, 8.0f, k_FixedDt);

    EXPECT_NEAR(entity.Velocity().x, 1.0f, 1e-5f);
    EXPECT_FLOAT_EQ(entity.Velocity().z, 0.0f);
}

TEST_F(EntityComponentTest, AccelerateKeepsVerticalVelocity)
{
    GameObject obj;
    BareEntity& entity = *obj.AddComponent<BareEntity>();
    entity.SetVelocity(Vector3{0.0f, -9.0f, 0.0f});

    entity.Accelerate(Vector3{1.0f, 0.0f, 0.0f}, 60.0f, 60.0f, 8.0f, k_FixedDt);

    EXPECT_FLOAT_EQ(entity.VerticalVelocity(), -9.0f);
}

TEST_F(EntityComponentTest, AccelerateStopsAtTheTopSpeed)
{
    GameObject obj;
    BareEntity& entity = *obj.AddComponent<BareEntity>();
    entity.SetVelocity(Vector3{7.9f, 0.0f, 0.0f});

    entity.Accelerate(Vector3{1.0f, 0.0f, 0.0f}, 60.0f, 60.0f, 8.0f, k_FixedDt);

    EXPECT_EQ(entity.Velocity().x, 8.0f);
}

TEST_F(EntityComponentTest, AccelerateRemovesSidewaysSpeedByTheTurningDrag)
{
    GameObject obj;
    BareEntity& entity = *obj.AddComponent<BareEntity>();
    entity.SetVelocity(Vector3{0.0f, 0.0f, 5.0f});

    entity.Accelerate(Vector3{1.0f, 0.0f, 0.0f}, 60.0f, 60.0f, 8.0f, k_FixedDt);

    EXPECT_NEAR(entity.Velocity().x, 1.0f, 1e-5f);
    EXPECT_NEAR(entity.Velocity().z, 4.0f, 1e-5f);
}

// 減速は一定量ずつ引き、ちょうど 0 で止まる
TEST_F(EntityComponentTest, DecelerateStopsAtExactlyZero)
{
    GameObject obj;
    BareEntity& entity = *obj.AddComponent<BareEntity>();
    entity.SetVelocity(Vector3{6.0f, 1.0f, -8.0f});

    entity.Decelerate(60.0f, k_FixedDt);
    EXPECT_NEAR(entity.Velocity().x, 5.4f, 1e-4f);
    EXPECT_NEAR(entity.Velocity().z, -7.2f, 1e-4f);
    EXPECT_FLOAT_EQ(entity.VerticalVelocity(), 1.0f);

    for (int i = 0; i < 10; ++i)
    {
        entity.Decelerate(60.0f, k_FixedDt);
    }
    EXPECT_EQ(entity.Velocity().x, 0.0f);
    EXPECT_EQ(entity.Velocity().z, 0.0f);
    EXPECT_FLOAT_EQ(entity.VerticalVelocity(), 1.0f);
}

// 減速が残す端数ほどの小さい水平の速さは 0 と読む。縦は残す
TEST_F(EntityComponentTest, LateralVelocityReadsARoundingRemainderAsZero)
{
    GameObject obj;
    BareEntity& entity = *obj.AddComponent<BareEntity>();
    entity.SetVelocity(Vector3{6e-7f, 3.0f, -6e-7f});

    EXPECT_EQ(entity.LateralVelocity().x, 0.0f);
    EXPECT_EQ(entity.LateralVelocity().z, 0.0f);
    EXPECT_FLOAT_EQ(entity.VerticalVelocity(), 3.0f);
}

TEST_F(EntityComponentTest, GravityChangesOnlyVerticalVelocity)
{
    GameObject obj;
    BareEntity& entity = *obj.AddComponent<BareEntity>();
    entity.SetVelocity(Vector3{3.0f, 0.0f, 4.0f});

    entity.Gravity(-25.0f, k_FixedDt);

    EXPECT_FLOAT_EQ(entity.Velocity().x, 3.0f);
    EXPECT_FLOAT_EQ(entity.Velocity().z, 4.0f);
    EXPECT_NEAR(entity.VerticalVelocity(), -25.0f * k_FixedDt, 1e-6f);
}
