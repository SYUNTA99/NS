#include "Game/Entity/EntityComponent.h"

#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/CapsuleCollider.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Gravity.h"
#include "Runtime/Object/Scene/Scene.h"
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
} // namespace

namespace NS::Game::Entity
{
    // 天井の当たりは持たない。JoltCharacter が接触面へ速度を射影するので、
    // 天井に当たったフレームの上向き速度は Move を抜けた時点で 0 になっている
    EntityComponent::EntityComponent() noexcept : NS::Obj::Component() {}

    NS::Core::Vector3 EntityComponent::LateralVelocity() const noexcept
    {
        // 8 m/s を減速度 40 で止めると、最後のフレームに 6×10⁻⁷ m/s の端数が残る
        // 0 と読まないと、止まったかの判定が 1 フレーム遅れる
        if (m_velocity.x * m_velocity.x + m_velocity.z * m_velocity.z < NS::Core::k_Epsilon * NS::Core::k_Epsilon)
        {
            return NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        }
        return NS::Core::Vector3{m_velocity.x, 0.0f, m_velocity.z};
    }

    void EntityComponent::SetLateralVelocity(const NS::Core::Vector3& v) noexcept
    {
        m_velocity.x = v.x;
        m_velocity.z = v.z;
    }

    void EntityComponent::SetGrounded(bool grounded) noexcept
    {
        m_wasGrounded = grounded;
        m_isGrounded = grounded;
    }

    float EntityComponent::CapsuleRadius() const noexcept
    {
        if (const NS::Obj::CapsuleCollider* capsule = SiblingCapsule())
        {
            return capsule->Radius();
        }
        return 0.4f;
    }

    float EntityComponent::CapsuleHalfHeight() const noexcept
    {
        // 移動と裁定はどちらもここから寸法を引くので、球にしている間は両方が同じ球で当たる
        if (m_sphereShape)
        {
            return 0.0f;
        }
        return StandingHalfHeight();
    }

    float EntityComponent::StandingHalfHeight() const noexcept
    {
        if (const NS::Obj::CapsuleCollider* capsule = SiblingCapsule())
        {
            return capsule->HalfHeight();
        }
        return 0.5f;
    }

    void EntityComponent::SetSphereShape(bool sphere) noexcept
    {
        m_sphereShape = sphere;
        SyncBodySensor();
    }

    void EntityComponent::SyncBodySensor() noexcept
    {
        if (m_bodySensor != nullptr)
        {
            m_bodySensor->SetCapsule(CapsuleRadius(), CapsuleHalfHeight());
        }
    }

    const NS::Obj::CapsuleCollider* EntityComponent::SiblingCapsule() const noexcept
    {
        if (m_capsuleCollider != nullptr)
        {
            return m_capsuleCollider;
        }
        // 資産の引き当ては OnStart より前に走る。PlayerAppearance がその時に聞いても移動と同じ当たりを返す
        if (Owner() == nullptr)
        {
            return nullptr;
        }
        return Owner()->ColliderPart();
    }

    NS::Phys::PhysicsScene* EntityComponent::ScenePhysics() const noexcept
    {
        if (Owner() == nullptr || Owner()->OwningScene() == nullptr)
        {
            return nullptr;
        }
        return &Owner()->OwningScene()->Physics();
    }

    void EntityComponent::OnStart()
    {
        if (Owner() != nullptr)
        {
            m_capsuleCollider = Owner()->ColliderPart();
        }
        // 自分の capsule は Move が掃引する。静的世界に居ると自分に当たって動けない
        if (m_capsuleCollider != nullptr)
        {
            m_capsuleCollider->SetExcludedFromStaticWorld(true);
        }
        // 体のセンサーは根を中心にした、移動の当たりと同じカプセル
        m_bodySensor = nullptr;
        if (Owner() != nullptr)
        {
            m_bodySensor = Owner()->BodySensorPart();
        }
        SyncBodySensor();
    }

    void EntityComponent::Accelerate(
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

    void EntityComponent::Decelerate(float deceleration, float dt) noexcept
    {
        NS::Core::Vector3 lateral = LateralVelocity();
        ShrinkHorizontal(lateral.x, lateral.z, deceleration * dt);
        m_velocity.x = lateral.x;
        m_velocity.z = lateral.z;
    }

    void EntityComponent::Gravity(float gravity, float dt) noexcept
    {
        const NS::Obj::Actor* owner = Owner();
        if (owner == nullptr)
        {
            m_velocity.y += gravity * dt;
            return;
        }
        NS::Obj::AddGravity(*owner, m_velocity, -gravity, dt);
    }

    void EntityComponent::Move(float dt, float maxStepHeight) noexcept
    {
        const NS::Core::Vector3 before = RootTransform().Position();
        NS::Phys::PhysicsScene* physics = ScenePhysics();
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
} // namespace NS::Game::Entity
