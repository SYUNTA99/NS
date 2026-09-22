#include "Game/Level/FollowCameraFeed.h"

#include "Game/Entity/EntityComponent.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/ThirdPersonFollow.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Scene/Scene.h"

#include <cstdint>

namespace NS::Game::Level
{
    // Respawner のやり直し (LateUpdate + 10) が済んだ後、
    // ThirdPersonFollow (LateUpdate + 50) が読む前に渡す
    FollowCameraFeed::FollowCameraFeed() noexcept
        : NS::Obj::Component(NS::Obj::TickPriority::LateUpdate + 40)
    {}

    void FollowCameraFeed::OnStart()
    {
        if (Owner() != nullptr)
        {
            m_entity = Owner()->FindComponent<NS::Game::Entity::EntityComponent>();
        }
        else
        {
            m_entity = nullptr;
        }
    }

    void FollowCameraFeed::OnUpdate()
    {
        if (m_entity == nullptr || Owner() == nullptr || Owner()->OwningScene() == nullptr)
        {
            return;
        }

        // 未採番の 0 同士を突き合わせると、誰も指していないカメラが持ち主を追っている扱いになる
        const std::uint32_t ownerId = Owner()->Id();
        if (ownerId == 0)
        {
            return;
        }

        const bool grounded = m_entity->IsGrounded();
        const NS::Core::Vector3 velocity = m_entity->Velocity();
        // カメラは控えず毎フレーム引き直す。控えると畳まれた相手を指したまま次のフレームへ持ち越す
        Owner()->OwningScene()->Objects().ForEachComponent<NS::Obj::ThirdPersonFollow>(
            [ownerId, grounded, &velocity](NS::Obj::ThirdPersonFollow& follow) {
                if (follow.TargetRef().id != ownerId)
                {
					return;
                }
                follow.SetFollowMotion(grounded, velocity);
            });
    }
} // namespace NS::Game::Level
