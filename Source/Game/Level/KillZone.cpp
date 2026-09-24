#include "Game/Level/KillZone.h"

#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Object/Components/BoxCollider.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Physics/PhysicsScene.h"

#include <algorithm>

namespace NS::Game::Level
{
    KillZone::KillZone() noexcept : NS::Obj::Component(NS::Obj::TickPriority::LateUpdate) {}

    void KillZone::OnUpdate()
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

    bool IsKillZoneObject(const nlohmann::json& object) noexcept
    {
        return NS::Obj::FindComponentEntry(object, "KillZone") != nullptr;
    }

    nlohmann::json MakeKillZoneObject()
    {
        // 上面 y=-50 は従来の落下死の高さ。厚み 10m と 2km 四方は固定ステップの移動量では突き抜けられない
        nlohmann::json box = NS::Obj::MakeComponentEntry("BoxCollider");
        NS::Obj::SetField(box, "半径", NS::Core::Vector3{1000.0f, 5.0f, 1000.0f});
        // トリガにしないと落ちてきたプレイヤーが上面に着地してしまう
        NS::Obj::SetField(box, "トリガー", true);
        nlohmann::json object =
            NS::Obj::MakeObjectJson(nlohmann::json::array({std::move(box), NS::Obj::MakeComponentEntry("KillZone")}));
        NS::Obj::SetObjectPosition(object, NS::Core::Vector3{0.0f, -55.0f, 0.0f});
        return object;
    }

    bool EnsureKillZoneObject(nlohmann::json& scene)
    {
        for (const nlohmann::json& object : NS::Obj::SceneJsonObjects(scene))
        {
            if (IsKillZoneObject(object))
                return false;
        }
        NS::Obj::SceneJsonObjects(scene).push_back(MakeKillZoneObject());
        NS::Obj::EnsureUniqueObjectIds(scene);
        return true;
    }

    NS_CLASS(KillZone)
} // namespace NS::Game::Level
