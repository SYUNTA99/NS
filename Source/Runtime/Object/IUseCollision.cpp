#include "Runtime/Object/IUseCollision.h"

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
} // namespace NS::Obj
