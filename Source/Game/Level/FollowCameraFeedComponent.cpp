#include "Game/Level/FollowCameraFeedComponent.h"

#include "Game/Entity/EntityComponent.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/ThirdPersonFollowComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/World.h"

#include <cstdint>

namespace NS::Game::Level
{
    // RespawnerComponent のやり直し (LateUpdate + 10) が済んだ後、
    // ThirdPersonFollowComponent (LateUpdate + 50) が読む前に渡す
    FollowCameraFeedComponent::FollowCameraFeedComponent() noexcept
        : NS::Object::Component(NS::Object::TickPriority::LateUpdate + 40)
    {}

    void FollowCameraFeedComponent::OnStart()
    {
        if (Owner() != nullptr)
            m_entity = Owner()->FindComponent<NS::Game::Entity::EntityComponent>();
        else
            m_entity = nullptr;
    }

    void FollowCameraFeedComponent::OnUpdate()
    {
        if (m_entity == nullptr || Owner() == nullptr || Owner()->OwningScene() == nullptr)
            return;

        // 未採番の 0 同士を突き合わせると、誰も指していないカメラが持ち主を追っている扱いになる
        const std::uint32_t ownerId = Owner()->Id();
        if (ownerId == 0)
            return;

        const bool grounded = m_entity->IsGrounded();
        const NS::Core::Vector3 velocity = m_entity->Velocity();
        // カメラは控えず毎歩引き直す。控えると畳まれた相手を指したまま次の歩へ持ち越す
        Owner()->OwningScene()->World().ForEachComponent<NS::Object::ThirdPersonFollowComponent>(
            [ownerId, grounded, &velocity](NS::Object::ThirdPersonFollowComponent& follow) {
                if (follow.TargetRef().id != ownerId)
                    return;
                follow.SetFollowMotion(grounded, velocity);
            });
    }
} // namespace NS::Game::Level
