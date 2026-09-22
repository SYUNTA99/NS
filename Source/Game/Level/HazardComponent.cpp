#include "Game/Level/HazardComponent.h"

#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Object/Components/BoxColliderComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Physics/PhysicsScene.h"

#include <algorithm>

namespace NS::Game::Level
{
    HazardComponent::HazardComponent() noexcept : NS::Obj::Component(NS::Obj::TickPriority::LateUpdate) {}

    void HazardComponent::OnUpdate()
    {
        auto* box = Owner()->FindComponent<NS::Obj::BoxColliderComponent>();
        if (box == nullptr)
        {
            return;
        }
 

        auto* scene = Owner()->OwningScene();
        if (scene == nullptr)
        {
			return;
        }
        // 組み直しで古い参照を掴むので、player は控えず毎ステップ引き直す
        auto* player = FindPlayer(scene->Objects());
        if (player == nullptr)
        {
            return;
        }
        auto* movement = player->FindComponent<NS::Game::Player::PlayerComponent>();
        if (movement == nullptr)
        {
            return;
        }

        const NS::Phys::Capsule capsule{player->Root().Position(),
                                           NS::Core::Vector3::UnitY,
                                           movement->CapsuleHalfHeight(),
                                           movement->CapsuleRadius()};
        const auto overlaps = scene->Physics().OverlapCapsule(capsule);
        if (std::find(overlaps.begin(), overlaps.end(), box->BodyId()) != overlaps.end())
        {
            player->ApplyDamage(1);
        }
    }

    NS_CLASS(HazardComponent)
} // namespace NS::Game::Level
