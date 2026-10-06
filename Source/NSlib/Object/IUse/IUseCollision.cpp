#include "NSlib/Object/IUse/IUseCollision.h"

#include "NSlib/Physics/PhysicsScene.h"

namespace NS::Obj
{
    bool RaycastCollision(const IUseCollision& user,
                          const NS::Vector3& origin,
                          const NS::Vector3& direction,
                          float maxDistance,
                          float& outDistance)
    {
        const NS::Phys::PhysicsScene* physics = user.GetPhysicsScene();
        return physics != nullptr && physics->Raycast(origin, direction, maxDistance, outDistance);
    }

    bool RaycastCollision(const IUseCollision& user,
                          const NS::Vector3& origin,
                          const NS::Vector3& direction,
                          float maxDistance,
                          float& outDistance,
                          NS::Vector3& outNormal,
                          JPH::BodyID ignoredBody)
    {
        const NS::Phys::PhysicsScene* physics = user.GetPhysicsScene();
        return physics != nullptr &&
               physics->Raycast(origin, direction, maxDistance, outDistance, outNormal, ignoredBody);
    }

    std::vector<NS::AABB> OverlapBoxCollision(const IUseCollision& user, const NS::AABB& region)
    {
        const NS::Phys::PhysicsScene* physics = user.GetPhysicsScene();
        if (physics == nullptr)
        {
            return {};
        }
        return physics->OverlapBox(region);
    }
} // namespace NS::Obj
