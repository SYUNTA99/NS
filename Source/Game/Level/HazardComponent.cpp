#include "Game/Level/HazardComponent.h"

#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Object/Components/BoxColliderComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/World.h"
#include "Runtime/Physics/Capsule.h"

namespace NS::Game::Level
{
    HazardComponent::HazardComponent() noexcept : NS::Object::Component(NS::Object::TickPriority::LateUpdate) {}

    void HazardComponent::OnUpdate()
    {
        auto* box = Owner()->FindComponent<NS::Object::BoxColliderComponent>();
        if (box == nullptr)
            return;

        auto* scene = Owner()->OwningScene();
        if (scene == nullptr)
            return;
        // 待ちまたぎの生ポインタを避けるため毎ステップ引き直す。組み直しで古い参照を掴む事故を防ぐ
        auto* player = FindPlayer(scene->World());
        if (player == nullptr)
            return;
        auto* movement = player->FindComponent<NS::Game::Player::PlayerComponent>();
        if (movement == nullptr)
            return;

        // 固形の衝突応答で capsule 中心は表面外に留まるため、軸線分から AABB の最近距離で重なりを見る
        NS::Physics::Capsule capsule{};
        capsule.center = player->Root().Position();
        capsule.radius = movement->CapsuleRadius();
        capsule.halfHeight = movement->CapsuleHalfHeight();
        if (NS::Physics::IntersectsCapsuleAABB(capsule, box->WorldAABB()))
            player->ApplyDamage(1);
    }

    NS_CLASS(HazardComponent)
} // namespace NS::Game::Level
