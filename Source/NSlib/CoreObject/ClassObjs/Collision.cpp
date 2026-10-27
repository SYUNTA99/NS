#include "NSlib/CoreObject/ClassObjs/Collision.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/IGet/IGetCollision.h"
#include "NSlib/Phys/PhysicsScene.h"

namespace NS
{
    Matrix Collision::ShapeWorldMatrix(const Matrix& world,
                                       const Quaternion& localRotation,
                                       const Vector3& centerOffset) noexcept
    {
        return Matrix::CreateFromQuaternion(localRotation) * Matrix::CreateTranslation(centerOffset) * world;
    }

    void Collision::SyncToPhysics(const Matrix& world, const IGetCollision* user)
    {
        NS_ASSERT(Scene, user != nullptr, "Collision::SyncToPhysics: 物理シーンの窓口が null");
        Phys::PhysicsScene* physics = user->GetPhysicsScene();
        if (physics == nullptr)
        {
            return;
        }

        // 置き直すたびに形を作り直し、broadphase も崩れるので、動かない当たりは置き直さない
        if (m_synced && !m_shapeChanged && world == m_syncedWorld)
        {
            return;
        }

        const JPH::BodyID body = SyncBody(*physics, m_bodyId, world);
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

    void Collision::RemoveFromPhysics(const IGetCollision* user)
    {
        NS_ASSERT(Scene, user != nullptr, "Collision::RemoveFromPhysics: 物理シーンの窓口が null");
        Phys::PhysicsScene* physics = user->GetPhysicsScene();
        if (physics == nullptr)
        {
            return;
        }
        physics->DetachBody(m_bodyId);
    }

    void Collision::ReturnToPhysics(const Matrix& world, const IGetCollision* user)
    {
        NS_ASSERT(Scene, user != nullptr, "Collision::ReturnToPhysics: 物理シーンの窓口が null");
        Phys::PhysicsScene* physics = user->GetPhysicsScene();
        if (physics == nullptr)
        {
            return;
        }
        physics->AttachBody(m_bodyId);
        // 外している間に動いた姿と変わった形へ揃える。body がまだ無ければここで作る
        SyncToPhysics(world, user);
    }

    void Collision::Unregister(const IGetCollision* user)
    {
        NS_ASSERT(Scene, user != nullptr, "Collision::Unregister: 物理シーンの窓口が null");
        if (m_bodyId.IsInvalid())
        {
            return;
        }

        Phys::PhysicsScene* physics = user->GetPhysicsScene();
        if (physics == nullptr)
        {
            NS_LOG_ERROR(Scene, "Collision: Scene に居ないので body を外せない。 body は PhysicsScene を壊すまで残る");
        }
        else
        {
            physics->RemoveBody(m_bodyId);
        }
        m_bodyId = JPH::BodyID{};
        m_synced = false;
    }
} // namespace NS
