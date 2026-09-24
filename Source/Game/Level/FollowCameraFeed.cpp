#include "Game/Level/FollowCameraFeed.h"

#include "Game/Entity/EntityComponent.h"
#include "Runtime/Object/Components/ThirdPersonFollow.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"

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
            m_follow = Owner()->FindComponent<NS::Obj::ThirdPersonFollow>();
        }
        else
        {
            m_follow = nullptr;
        }
    }

    void FollowCameraFeed::OnUpdate()
    {
        if (m_follow == nullptr || Owner()->OwningScene() == nullptr)
        {
            return;
        }

        // 追う相手は控えず毎フレーム引き直す。控えると、消された相手を指したまま次のフレームへ持ち越す
        NS::Obj::GameObject* target = Owner()->OwningScene()->Objects().FindObject(m_follow->TargetRef());
        if (target == nullptr)
        {
            return;
        }
        const auto* entity = target->FindComponent<NS::Game::Entity::EntityComponent>();
        if (entity == nullptr)
        {
            return;
        }
        m_follow->SetFollowMotion(entity->IsGrounded(), entity->Velocity());
    }

    NS_CLASS(FollowCameraFeed)
} // namespace NS::Game::Level
