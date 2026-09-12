#include "Game/Level/KillZoneComponent.h"

#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Object/Components/BoxColliderComponent.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Physics/PhysicsWorld.h"

#include <algorithm>

namespace NS::Game::Level
{
    KillZoneComponent::KillZoneComponent() noexcept : NS::Object::Component(NS::Object::TickPriority::LateUpdate) {}

    void KillZoneComponent::OnUpdate()
    {
        auto* box = Owner()->FindComponent<NS::Object::BoxColliderComponent>();
        if (box == nullptr)
            return;

        auto* scene = Owner()->OwningScene();
        if (scene == nullptr)
            return;
        auto* player = FindPlayer(scene->Objects());
        if (player == nullptr)
            return;
        auto* movement = player->FindComponent<NS::Game::Player::PlayerComponent>();
        if (movement == nullptr)
            return;

        const NS::Physics::Capsule capsule{player->Root().Position(),
                                           NS::Core::Vector3::UnitY,
                                           movement->CapsuleHalfHeight(),
                                           movement->CapsuleRadius()};
        const auto overlaps = scene->Physics().OverlapCapsule(capsule);
        if (std::find(overlaps.begin(), overlaps.end(), box->BodyId()) != overlaps.end())
            player->Kill();
    }

    bool IsKillZoneObject(const NS::Object::ObjectData& object) noexcept
    {
        return NS::Object::FindComponentEntry(object, "KillZoneComponent") != nullptr;
    }

    NS::Object::ObjectData MakeKillZoneObject()
    {
        // 上面 y=-50 は従来の落下死の高さ。厚み 10m と 2km 四方は固定ステップの移動量では突き抜けられない
        NS::Object::ObjectData object{};
        nlohmann::json box = NS::Object::MakeComponentEntry("BoxColliderComponent");
        NS::Object::SetField(box, "半径", NS::Core::Vector3{1000.0f, 5.0f, 1000.0f});
        // トリガにしないと落ちてきたプレイヤーが上面に着地してしまう
        NS::Object::SetField(box, "トリガー", true);
        object.components =
            nlohmann::json::array({std::move(box), NS::Object::MakeComponentEntry("KillZoneComponent")});
        NS::Object::SetObjectPosition(object, NS::Core::Vector3{0.0f, -55.0f, 0.0f});
        return object;
    }

    bool EnsureKillZoneObject(NS::Object::SceneData& level)
    {
        for (const NS::Object::ObjectData& object : level.objects)
        {
            if (IsKillZoneObject(object))
                return false;
        }
        level.objects.push_back(MakeKillZoneObject());
        NS::Object::EnsureUniqueObjectIds(level);
        return true;
    }

    NS_CLASS(KillZoneComponent)
} // namespace NS::Game::Level
