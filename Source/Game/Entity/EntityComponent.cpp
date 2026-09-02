#include "Game/Entity/EntityComponent.h"

#include "Runtime/Core/Clock.h"
#include "Runtime/Object/Components/CapsuleColliderComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/PhysicsWorld.h"

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
    // 天井の当たりは持たない。CapsuleMover が接触面へ速度を射影するので、
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
        // 当たりの形の正は同居する CapsuleColliderComponent。掃引はこの写しを読むので毎歩追従させる
        if (m_capsuleCollider != nullptr)
        {
            m_capsuleRadius = m_capsuleCollider->Radius();
            m_capsuleHalfHeight = m_capsuleCollider->HalfHeight();
        }

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

        NS::Physics::CapsuleMoverInput in{};
        in.position = before;
        in.velocity = m_velocity;
        in.dt = dt;
        in.capsuleRadius = m_capsuleRadius;
        in.capsuleHalfHeight = m_capsuleHalfHeight;
        in.physicsWorld = m_world;
        const NS::Physics::CapsuleMoverResult out = m_controller.Update(in);

        RootTransform().SetPosition(out.position);
        m_velocity = out.velocity;
        m_wasGrounded = m_isGrounded;
        m_isGrounded = out.grounded;
        m_positionDelta = out.position - before;

        // 発火は位置・速度・接地・実移動を書き終えた後。途中で呼ぶと購読側がその歩だけ古い値を読む
        if (!m_wasGrounded && m_isGrounded)
            m_events.onGroundEnter.Invoke();
        else if (m_wasGrounded && !m_isGrounded)
            m_events.onGroundExit.Invoke();
    }
} // namespace NS::Game::Entity
