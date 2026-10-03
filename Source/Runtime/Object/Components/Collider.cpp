#include "Runtime/Object/Components/Collider.h"

#include "Runtime/Core/Logger.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Physics/PhysicsScene.h"

namespace
{
    // 持ち主の Scene の PhysicsScene。持ち主が無いか Scene に居なければ nullptr
    [[nodiscard]] NS::Phys::PhysicsScene* OwnerPhysics(const NS::Obj::Actor* owner) noexcept
    {
        if (owner == nullptr)
        {
            return nullptr;
        }
        return owner->GetPhysicsScene();
    }
} // namespace

namespace NS::Obj
{
    void Collider::SyncToPhysics()
    {
        NS::Phys::PhysicsScene* physics = OwnerPhysics(Owner());
        if (physics == nullptr)
        {
            return;
        }

        const JPH::BodyID body = SyncBody(*physics, m_bodyId);
        // 置き直しは同じ id を返す。違うのは初めて作った時か、無効が返った時だけ
        if (m_bodyId != body)
        {
            physics->RemoveBody(m_bodyId);
        }
        m_bodyId = body;
    }

    void Collider::RemoveFromPhysics()
    {
        NS::Phys::PhysicsScene* physics = OwnerPhysics(Owner());
        if (physics == nullptr)
        {
            return;
        }

        physics->RemoveBody(m_bodyId);
        m_bodyId = JPH::BodyID{};
    }

    void Collider::OnAppear()
    {
        SyncToPhysics();
    }

    void Collider::OnEndPlay()
    {
        if (m_bodyId.IsInvalid())
        {
            return;
        }

        if (OwnerPhysics(Owner()) == nullptr)
        {
            NS_LOG_ERROR(Scene, "Collider: Scene に居ないので body を外せない。 body は PhysicsScene を壊すまで残る");
            m_bodyId = JPH::BodyID{};
            return;
        }
        RemoveFromPhysics();
    }
} // namespace NS::Obj
