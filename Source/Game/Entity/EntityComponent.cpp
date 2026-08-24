#include "Game/Entity/EntityComponent.h"

#include "Runtime/Core/Clock.h"
#include "Runtime/Object/Components/CapsuleColliderComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/PhysicsWorld.h"

namespace NS::Game::Entity
{
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
    }

    void EntityComponent::OnUpdate()
    {
        // 当たりの形の正は同居する CapsuleColliderComponent。写さないと Inspector で触っても移動に効かない
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

    void EntityComponent::Move(float dt) noexcept
    {
        NS::Physics::CapsuleMoverInput in{};
        in.position = RootTransform().Position();
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
    }
} // namespace NS::Game::Entity
