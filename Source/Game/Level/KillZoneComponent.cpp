#include "Game/Level/KillZoneComponent.h"

#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Object/Components/BoxCollider.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Physics/PhysicsScene.h"

#include <algorithm>

namespace NS::Game::Level
{
    KillZoneComponent::KillZoneComponent() noexcept : NS::Obj::Component(NS::Obj::TickPriority::LateUpdate) {}

    void KillZoneComponent::OnUpdate()
    {
        NS::Obj::BoxCollider* box = Owner()->FindComponent<NS::Obj::BoxCollider>();
        if (box == nullptr)
            return;

        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
            return;
        ::Player* player = FindPlayer(scene->Objects());
        if (player == nullptr)
            return;
        NS::Game::Player::PlayerComponent* movement = player->FindComponent<NS::Game::Player::PlayerComponent>();
        if (movement == nullptr)
            return;

        const NS::Phys::Capsule capsule{player->Root().Position(),
                                           NS::Core::Vector3::UnitY,
                                           movement->CapsuleHalfHeight(),
                                           movement->CapsuleRadius()};
        const std::vector<JPH::BodyID> overlaps = scene->Physics().OverlapCapsule(capsule);
        if (std::find(overlaps.begin(), overlaps.end(), box->BodyId()) != overlaps.end())
            player->Kill();
    }

    NS_CLASS(KillZoneComponent)
} // namespace NS::Game::Level
