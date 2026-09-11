#include "Game/Entity/EntityComponent.h"

#include "Runtime/Core/Clock.h"
#include "Runtime/Object/Components/CapsuleColliderComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/JoltCharacter.h"

#include <cmath>

namespace
{
    //! 一次遅れの離散化。tau は時定数で値が大きいほど鈍い、dt は step。0 < tau で安定
    //! 加速と減速の手触りはこの式が決める
    [[nodiscard]] float SmoothApproach(float current, float target, float tau, float dt) noexcept
    {
        if (tau <= 0.0f)
            return target;
        const float a = 1.0f - std::exp(-dt / tau);
        return current + (target - current) * a;
    }

    [[nodiscard]] NS::Core::Vector3 HorizontalSmooth(const NS::Core::Vector3& curr,
                                                     const NS::Core::Vector3& target,
                                                     float tau,
                                                     float dt) noexcept
    {
        return NS::Core::Vector3{
            SmoothApproach(curr.x, target.x, tau, dt),
            curr.y,
            SmoothApproach(curr.z, target.z, tau, dt),
        };
    }
} // namespace

namespace NS::Game::Entity
{
    // 天井の当たりは持たない。JoltCharacter が接触面へ速度を射影するので、
    // 天井に当たった歩の上向き速度は Move を抜けた時点で 0 になっている
    EntityComponent::EntityComponent() noexcept : NS::Object::Component(NS::Object::TickPriority::Update) {}

    NS::Core::Vector3 EntityComponent::LateralVelocity() const noexcept
    {
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
        return m_capsuleCollider != nullptr ? m_capsuleCollider->Radius() : 0.4f;
    }

    float EntityComponent::CapsuleHalfHeight() const noexcept
    {
        return m_capsuleCollider != nullptr ? m_capsuleCollider->HalfHeight() : 0.5f;
    }

    void EntityComponent::SetPhysicsWorld(NS::Physics::PhysicsWorld* world) noexcept
    {
        m_world = world;
        m_character.reset();
    }

    void EntityComponent::OnStart()
    {
        if (m_world == nullptr && Owner() != nullptr && Owner()->OwningScene() != nullptr)
            m_world = &Owner()->OwningScene()->Physics();
        if (Owner() != nullptr)
            m_capsuleCollider = Owner()->FindComponent<NS::Object::CapsuleColliderComponent>();
        // 自分の capsule は Move が掃引する。静的世界に居ると自分に当たって動けない
        if (m_capsuleCollider != nullptr)
            m_capsuleCollider->SetExcludedFromStaticWorld(true);
    }

    void EntityComponent::OnUpdate()
    {
        const float dt = NS::Core::FrameTimer::FixedDelta();

        if (!IsActive() || dt <= 0.0f)
        {
            OnStepSkipped();
            return;
        }

        HandleStates(dt);
    }

    void EntityComponent::Accelerate(const NS::Core::Vector3& targetHorizontal, float tau, float dt) noexcept
    {
        m_velocity = HorizontalSmooth(m_velocity, targetHorizontal, tau, dt);
    }

    void EntityComponent::Decelerate(float tau, float dt) noexcept
    {
        Accelerate(NS::Core::Vector3{0.0f, 0.0f, 0.0f}, tau, dt);
    }

    void EntityComponent::Gravity(float gravity, float dt) noexcept
    {
        m_velocity.y += gravity * dt;
    }

    void EntityComponent::Move(float dt) noexcept
    {
        const NS::Core::Vector3 before = RootTransform().Position();
        if (m_world == nullptr)
        {
            RootTransform().SetPosition(before + m_velocity * dt);
            m_wasGrounded = m_isGrounded;
            m_isGrounded = false;
            return;
        }

        const float radius = CapsuleRadius();
        const float halfHeight = CapsuleHalfHeight();
        if (m_character == nullptr)
            m_character = std::make_unique<NS::Physics::JoltCharacter>(*m_world, radius, halfHeight);
        m_character->Resize(radius, halfHeight);

        m_character->Step(before, m_velocity, dt);

        RootTransform().SetPosition(m_character->Position());
        m_velocity = m_character->Velocity();
        m_wasGrounded = m_isGrounded;
        m_isGrounded = m_character->IsGrounded();

        // 発火は位置・速度・接地を書き終えた後。途中で呼ぶと購読側がその歩だけ古い値を読む
        if (!m_wasGrounded && m_isGrounded)
            m_events.onGroundEnter.Invoke();
        else if (m_wasGrounded && !m_isGrounded)
            m_events.onGroundExit.Invoke();
    }
} // namespace NS::Game::Entity
