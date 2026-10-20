#include "NSlib/CoreObject/ClassObjs/Collision.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Phys/PhysicsScene.h"

namespace
{
    // 持ち主の Scene の PhysicsScene。持ち主が無いか Scene に居なければ nullptr
    [[nodiscard]] NS::Phys::PhysicsScene* OwnerPhysics(const NS::SceneActor* owner) noexcept
    {
        if (owner == nullptr)
        {
            return nullptr;
        }
        return owner->GetPhysicsScene();
    }
} // namespace

namespace NS
{
    Matrix Collision::ShapeWorldMatrix(const Quaternion& localRotation,
                                           const Vector3& centerOffset) const noexcept
    {
        const Matrix local =
            Matrix::CreateFromQuaternion(localRotation) * Matrix::CreateTranslation(centerOffset);
        if (m_owner == nullptr)
        {
            return local;
        }
        return local * m_owner->Root().WorldMatrix();
    }

    void Collision::SyncToPhysics()
    {
        Phys::PhysicsScene* physics = OwnerPhysics(m_owner);
        if (physics == nullptr)
        {
            return;
        }

        // 消えている持ち主の当たりは消えた時に外れている。入れ直すのは出る時
        if (!m_owner->IsAppeared())
        {
            return;
        }
        // 置き直すたびに形を作り直し、broadphase も崩れるので、動かない当たりは置き直さない
        const Matrix world = m_owner->Root().WorldMatrix();
        if (m_synced && !m_shapeChanged && world == m_syncedWorld)
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
        m_syncedWorld = world;
        m_synced = true;
        m_shapeChanged = false;
    }

    void Collision::RemoveFromPhysics()
    {
        Phys::PhysicsScene* physics = OwnerPhysics(m_owner);
        if (physics == nullptr)
        {
            return;
        }

        physics->RemoveBody(m_bodyId);
        m_bodyId = JPH::BodyID{};
        m_synced = false;
    }

    void Collision::Unregister()
    {
        if (m_bodyId.IsInvalid())
        {
            return;
        }

        if (OwnerPhysics(m_owner) == nullptr)
        {
            NS_LOG_ERROR(Scene, "Collision: Scene に居ないので body を外せない。 body は PhysicsScene を壊すまで残る");
            m_bodyId = JPH::BodyID{};
            m_synced = false;
            return;
        }
        RemoveFromPhysics();
    }
} // namespace NS
