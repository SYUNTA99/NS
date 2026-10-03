#include "Runtime/Object/Components/Body.h"

#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Gravity.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/JoltCharacter.h"

#include <cmath>

namespace
{
    // 水平の長さを drop だけ縮め、drop 以下なら 0 にする
    void ShrinkHorizontal(float& x, float& z, float drop) noexcept
    {
        const float length = std::sqrt(x * x + z * z);
        if (length <= drop || length == 0.0f)
        {
            x = 0.0f;
            z = 0.0f;
            return;
        }
        const float scale = (length - drop) / length;
        x *= scale;
        z *= scale;
    }

    // 寸法の欄へ書く値。負は 0、有限でなければ書く前の値
    float NonNegativeLength(float value, float current) noexcept
    {
        if (!std::isfinite(value))
        {
            return current;
        }
        if (value < 0.0f)
        {
            return 0.0f;
        }
        return value;
    }
} // namespace

namespace NS::Obj
{
    // 天井の当たりは持たない。JoltCharacter が接触面へ速度を射影するので、
    // 天井に当たったフレームの上向き速度は Move を抜けた時点で 0 になっている
    Body::Body() noexcept : NS::Obj::Component() {}

    NS::Core::Vector3 Body::LateralVelocity() const noexcept
    {
        // 8 m/s を減速度 40 で止めると、最後のフレームに 6×10⁻⁷ m/s の端数が残る
        // 0 と読まないと、止まったかの判定が 1 フレーム遅れる
        if (m_velocity.x * m_velocity.x + m_velocity.z * m_velocity.z < NS::Core::k_Epsilon * NS::Core::k_Epsilon)
        {
            return NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        }
        return NS::Core::Vector3{m_velocity.x, 0.0f, m_velocity.z};
    }

    void Body::SetLateralVelocity(const NS::Core::Vector3& v) noexcept
    {
        m_velocity.x = v.x;
        m_velocity.z = v.z;
    }

    void Body::SetGrounded(bool grounded) noexcept
    {
        m_wasGrounded = grounded;
        m_isGrounded = grounded;
    }

    void Body::SetCapsuleRadius(float radius) noexcept
    {
        m_radius = NonNegativeLength(radius, m_radius);
        SyncBodySensor();
    }

    void Body::SetStandingHalfHeight(float halfHeight) noexcept
    {
        m_standingHalfHeight = NonNegativeLength(halfHeight, m_standingHalfHeight);
        SyncBodySensor();
    }

    float Body::CapsuleHalfHeight() const noexcept
    {
        // 移動と裁定はどちらもここから寸法を引くので、球にしている間は両方が同じ球で当たる
        if (m_sphereShape)
        {
            return 0.0f;
        }
        return StandingHalfHeight();
    }

    NS::Phys::Capsule Body::CapsuleAt(const NS::Core::Vector3& rootPosition) const noexcept
    {
        // 軸は +Y 固定、中心は根。Move が JoltCharacter へ渡す (半径, 半分の高さ) と同じ 2 つの値から組む
        return NS::Phys::Capsule{rootPosition, NS::Core::Vector3::UnitY, CapsuleHalfHeight(), CapsuleRadius()};
    }

    void Body::SetSphereShape(bool sphere) noexcept
    {
        m_sphereShape = sphere;
        SyncBodySensor();
    }

    void Body::SyncBodySensor() noexcept
    {
        if (m_bodySensor != nullptr)
        {
            m_bodySensor->SetCapsule(CapsuleRadius(), CapsuleHalfHeight());
        }
    }

    NS::Phys::PhysicsScene* Body::GetPhysicsScene() const noexcept
    {
        if (Owner() == nullptr)
        {
            return nullptr;
        }
        return Owner()->GetPhysicsScene();
    }

    void Body::OnStart()
    {
        // 体のセンサーは根を中心にした、移動の当たりと同じカプセル
        m_bodySensor = nullptr;
        if (Owner() != nullptr)
        {
            m_bodySensor = ComponentCast<ShapeHitSensor>(Owner()->BodySensorPart());
        }
        SyncBodySensor();
    }

    void Body::Accelerate(
        const NS::Core::Vector3& direction, float turningDrag, float acceleration, float topSpeed, float dt) noexcept
    {
        const NS::Core::Vector3 lateral = LateralVelocity();
        float speed = direction.x * lateral.x + direction.z * lateral.z;
        float turningX = lateral.x - direction.x * speed;
        float turningZ = lateral.z - direction.z * speed;

        const float lateralSpeed = std::sqrt(lateral.x * lateral.x + lateral.z * lateral.z);
        if (lateralSpeed < topSpeed || speed < 0.0f)
        {
            speed = NS::Core::Clamp(speed + acceleration * dt, -topSpeed, topSpeed);
        }

        ShrinkHorizontal(turningX, turningZ, turningDrag * dt);
        m_velocity.x = direction.x * speed + turningX;
        m_velocity.z = direction.z * speed + turningZ;
    }

    void Body::Decelerate(float deceleration, float dt) noexcept
    {
        NS::Core::Vector3 lateral = LateralVelocity();
        ShrinkHorizontal(lateral.x, lateral.z, deceleration * dt);
        m_velocity.x = lateral.x;
        m_velocity.z = lateral.z;
    }

    void Body::Gravity(float gravity, float dt) noexcept
    {
        const NS::Obj::Actor* owner = Owner();
        if (owner == nullptr)
        {
            m_velocity.y += gravity * dt;
            return;
        }
        NS::Obj::AddGravity(*owner, m_velocity, -gravity, dt);
    }

    void Body::Move(float dt, float maxStepHeight) noexcept
    {
        const NS::Core::Vector3 before = RootTransform().Position();
        NS::Phys::PhysicsScene* physics = GetPhysicsScene();
        if (physics == nullptr)
        {
            RootTransform().SetPosition(before + m_velocity * dt);
            m_wasGrounded = m_isGrounded;
            m_isGrounded = false;
            return;
        }

        const float radius = CapsuleRadius();
        const float halfHeight = CapsuleHalfHeight();
        // AttachScene は新しく組んだ配置物にしか呼ばれない。Scene が変わらないので m_character を作り直さない
        if (m_character == nullptr)
        {
            m_character = std::make_unique<NS::Phys::JoltCharacter>(*physics, radius, halfHeight);
        }
        m_character->Resize(radius, halfHeight);

        m_character->Step(before, m_velocity, dt, maxStepHeight);

        RootTransform().SetPosition(m_character->Position());
        m_velocity = m_character->Velocity();
        m_wasGrounded = m_isGrounded;
        m_isGrounded = m_character->IsGrounded();

        // 発火は位置・速度・接地を書き終えた後。途中で呼ぶと購読側がそのフレームだけ古い値を読む
        if (!m_wasGrounded && m_isGrounded)
        {
            m_events.onGroundEnter.Invoke();
        }
        else if (m_wasGrounded && !m_isGrounded)
        {
            m_events.onGroundExit.Invoke();
        }
    }
} // namespace NS::Obj
