#include "Runtime/Object/Components/Collider.h"

#include "Runtime/Core/Logger.h"
#include "Runtime/Object/Components/RigidBody.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Physics/PhysicsScene.h"

namespace NS::Obj
{
    void Collider::SyncToPhysics(NS::Phys::PhysicsScene& physics)
    {
        if (!AcceptsScenePhysics(physics))
        {
            return;
        }

        // 形は RigidBody が body へまとめる。自分の body を残すと、同じ場所に静的な当たりが二重に立つ
        // 物理に入れない当たりは、そうする前に作った body もここで外す
        if (m_excludedFromPhysics || JoinsRigidBody())
        {
            physics.RemoveBody(m_bodyId);
            m_bodyId = JPH::BodyID{};
            return;
        }

        // トリガーは Trigger の層に置き、どの層とも組ませない。sensor にするのは重なりの問い合わせにだけ出すため
        JPH::ObjectLayer layer = NS::Phys::ObjectLayers::Terrain;
        if (m_isTrigger)
        {
            layer = NS::Phys::ObjectLayers::Trigger;
        }
        const JPH::BodyID body = SyncBody(physics, m_bodyId, layer, m_isTrigger);
        // 置き直しは同じ id を返す。違うのは初めて作った時か、無効が返った時だけ
        if (m_bodyId != body)
        {
            physics.RemoveBody(m_bodyId);
        }
        m_bodyId = body;
    }

    JPH::BodyID Collider::BodyId() const noexcept
    {
        if (m_bodyId.IsInvalid() && JoinsRigidBody())
        {
            return Owner()->FindComponent<RigidBody>()->BodyId();
        }
        return m_bodyId;
    }

    void Collider::SetTrigger(bool isTrigger) noexcept
    {
        m_isTrigger = isTrigger;
    }

    bool Collider::IsTrigger() const noexcept
    {
        return m_isTrigger;
    }

    void Collider::SetExcludedFromPhysics(bool excluded) noexcept
    {
        m_excludedFromPhysics = excluded;
    }

    bool Collider::IsExcludedFromPhysics() const noexcept
    {
        return m_excludedFromPhysics;
    }

    bool Collider::CanJoinRigidBody() const noexcept
    {
        return !m_isTrigger && !m_excludedFromPhysics && ShapeCanJoinRigidBody();
    }

    bool Collider::FollowsRigidBody() const noexcept
    {
        return m_isTrigger && !m_excludedFromPhysics;
    }

    bool Collider::JoinsRigidBody() const noexcept
    {
        const GameObject* owner = Owner();
        if (owner == nullptr || !CanJoinRigidBody())
        {
            return false;
        }
        const RigidBody* rigidBody = owner->FindComponent<RigidBody>();
        return rigidBody != nullptr && rigidBody->IsActive();
    }

    void Collider::RemoveFromPhysics(NS::Phys::PhysicsScene& physics)
    {
        if (!AcceptsScenePhysics(physics))
        {
            return;
        }

        physics.RemoveBody(m_bodyId);
        m_bodyId = JPH::BodyID{};
    }

    void Collider::OnEndPlay()
    {
        if (m_bodyId.IsInvalid())
        {
            return;
        }

        NS::Phys::PhysicsScene* scenePhysics = ScenePhysics();
        if (scenePhysics == nullptr)
        {
            NS_LOG_ERROR(Scene, "Collider: Scene に居ないので body を外せない。 body は PhysicsScene を壊すまで残る");
            m_bodyId = JPH::BodyID{};
            return;
        }
        RemoveFromPhysics(*scenePhysics);
    }

    NS::Phys::PhysicsScene* Collider::ScenePhysics() const noexcept
    {
        const GameObject* owner = Owner();
        if (owner == nullptr || owner->OwningScene() == nullptr)
        {
            return nullptr;
        }
        return &owner->OwningScene()->Physics();
    }

    bool Collider::AcceptsScenePhysics(const NS::Phys::PhysicsScene& physics) const
    {
        const NS::Phys::PhysicsScene* scenePhysics = ScenePhysics();
        if (scenePhysics == nullptr)
        {
            NS_LOG_ERROR(Scene, "Collider: 持ち主が Scene に居ないので body を出し入れしない");
            return false;
        }
        if (scenePhysics == &physics)
        {
            return true;
        }

        NS_LOG_ERROR(Scene, "Collider: 持ち主の Scene と違う PhysicsScene を渡された。 body を出し入れしない");
        return false;
    }
} // namespace NS::Obj
