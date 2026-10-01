#include "Runtime/Object/IUse/IUseCollision.h"

#include "Runtime/Physics/PhysicsScene.h"

namespace NS::Obj
{
    bool RaycastCollision(const IUseCollision& user,
                          const NS::Core::Vector3& origin,
                          const NS::Core::Vector3& direction,
                          float maxDistance,
                          float& outDistance)
    {
        const NS::Phys::PhysicsScene* physics = user.GetPhysicsScene();
        return physics != nullptr && physics->Raycast(origin, direction, maxDistance, outDistance);
    }

    bool RaycastCollision(const IUseCollision& user,
                          const NS::Core::Vector3& origin,
                          const NS::Core::Vector3& direction,
                          float maxDistance,
                          float& outDistance,
                          NS::Core::Vector3& outNormal,
                          JPH::BodyID ignoredBody)
    {
        const NS::Phys::PhysicsScene* physics = user.GetPhysicsScene();
        return physics != nullptr &&
               physics->Raycast(origin, direction, maxDistance, outDistance, outNormal, ignoredBody);
    }
} // namespace NS::Obj
