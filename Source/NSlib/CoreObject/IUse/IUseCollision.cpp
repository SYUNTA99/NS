#include "NSlib/CoreObject/IUse/IUseCollision.h"
#include "NSlib/Core/Assert.h"

#include "NSlib/Physics/PhysicsScene.h"

namespace NS
{
    bool RaycastCollision(const IUseCollision* user,
                          const Vector3& origin,
                          const Vector3& direction,
                          float maxDistance,
                          float& outDistance)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const Phys::PhysicsScene* physics = user->GetPhysicsScene();
        return physics != nullptr && physics->Raycast(origin, direction, maxDistance, outDistance);
    }

    bool RaycastCollision(const IUseCollision* user,
                          const Vector3& origin,
                          const Vector3& direction,
                          float maxDistance,
                          float& outDistance,
                          Vector3& outNormal,
                          JPH::BodyID ignoredBody)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const Phys::PhysicsScene* physics = user->GetPhysicsScene();
        return physics != nullptr &&
               physics->Raycast(origin, direction, maxDistance, outDistance, outNormal, ignoredBody);
    }

    std::vector<AABB> OverlapBoxCollision(const IUseCollision* user, const AABB& region)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const Phys::PhysicsScene* physics = user->GetPhysicsScene();
        if (physics == nullptr)
        {
            return {};
        }
        return physics->OverlapBox(region);
    }
} // namespace NS
