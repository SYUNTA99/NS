#include "Game/Level/MapObj.h"

#include "Game/Level/Breakable.h"
#include "Game/Level/LaunchEffects.h"
#include "Game/Level/LaunchedBody.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/TackleReaction.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Components/RigidBody.h"
#include "Runtime/Object/Components/SphereCollider.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/SceneJson.h"

namespace NS::Game::Level
{
    MapObj::MapObj() noexcept
    {
        NS::Obj::MeshRenderer* mesh = AddComponent<NS::Obj::MeshRenderer>();
        mesh->SetMeshRef("sphere");
        mesh->SetBaseColor(NS::Core::Vector3{0.72f, 0.70f, 0.66f});
        AddComponent<NS::Obj::SphereCollider>();
        // 置かれている間は動かない。押し飛ばされた時だけ LaunchedBody がダイナミックにする
        NS::Obj::RigidBody* body = AddComponent<NS::Obj::RigidBody>();
        body->SetKinematic(true);
        AddComponent<Breakable>();
        AddComponent<LaunchedBody>();
        // 体当たりの受け方と、飛ばされた時に自分で出す尾
        AddComponent<TackleReaction>();
        AddComponent<LaunchEffects>();
        // 物の体。プレイヤーの体当たりに調べられる。形は開始の後に当たりの球へ合わせる
        NS::Obj::HitSensor* sensor = AddComponent<NS::Obj::HitSensor>();
        sensor->SetType(NS::Obj::HitSensorType::MapObjBody);
        sensor->SetSphere(0.5f);
        // 影は種類の既定値 (Assets/Archetypes/MapObj.json) が足す。影を落とさない置物も同じクラスで作れる
    }

    void MapObj::InitAfterPlacement()
    {
        // 体当たりが調べる体と、地形とぶつかる当たりを同じ球にする。当たりの大きさを変えても体が付いて来る
        NS::Obj::HitSensor* body = FindComponent<NS::Obj::HitSensor>();
        const NS::Obj::SphereCollider* sphere = FindComponent<NS::Obj::SphereCollider>();
        if (body != nullptr && sphere != nullptr)
        {
            body->SetSphere(sphere->Radius());
            body->SetCenterOffset(sphere->CenterOffset());
        }
    }

    bool MapObj::ReceiveMsg(const NS::Obj::Message& msg, NS::Obj::HitSensor* sender, NS::Obj::HitSensor* receiver)
    {
        (void)sender;
        (void)receiver;
        TackleReaction* reaction = FindComponent<TackleReaction>();
        if (const MsgAskTackleTarget* ask = NS::Obj::MsgCast<MsgAskTackleTarget>(msg))
        {
            return reaction != nullptr && reaction->Answer(ask->Answer());
        }
        if (const MsgTackleFreeze* freeze = NS::Obj::MsgCast<MsgTackleFreeze>(msg))
        {
            if (reaction == nullptr)
            {
                return false;
            }
            reaction->BeginFreeze(freeze->Desc());
            return true;
        }
        if (const MsgTackleRelease* release = NS::Obj::MsgCast<MsgTackleRelease>(msg))
        {
            if (reaction == nullptr)
            {
                return false;
            }
            reaction->Release(release->Desc());
            return true;
        }
        if (const MsgCourseRestart* restart = NS::Obj::MsgCast<MsgCourseRestart>(msg))
        {
            // 飛ばされた物も置いた所へ戻し、コースを同じ形でやり直させる。凍結に居なければ応じない
            const nlohmann::json& baseline = restart->Baseline();
            const std::size_t index = NS::Obj::FindObjectIndexById(baseline, Id());
            LaunchedBody* body = FindComponent<LaunchedBody>();
            if (index == NS::Obj::k_NoObjectIndex || body == nullptr)
            {
                return false;
            }
            const nlohmann::json& placed = NS::Obj::SceneJsonObjects(baseline)[index];
            body->ResetTo(NS::Obj::ObjectPosition(placed), NS::Obj::ObjectRotation(placed));
            return true;
        }
        return false;
    }

    NS_PLACEABLE(MapObj, "置物")
} // namespace NS::Game::Level
