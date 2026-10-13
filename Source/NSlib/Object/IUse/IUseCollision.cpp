#include "NSlib/Object/IUse/IUseCollision.h"
#include "NSlib/Core/Assert.h"

#include "NSlib/Physics/PhysicsScene.h"

namespace NS::Obj
{
    bool RaycastCollision(const IUseCollision* user,
                          const NS::Vector3& origin,
                          const NS::Vector3& direction,
                          float maxDistance,
                          float& outDistance)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const NS::Phys::PhysicsScene* physics = user->GetPhysicsScene();
        return physics != nullptr && physics->Raycast(origin, direction, maxDistance, outDistance);
    }

    bool RaycastCollision(const IUseCollision* user,
                          const NS::Vector3& origin,
                          const NS::Vector3& direction,
                          float maxDistance,
                          float& outDistance,
                          NS::Vector3& outNormal,
                          JPH::BodyID ignoredBody)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const NS::Phys::PhysicsScene* physics = user->GetPhysicsScene();
        return physics != nullptr &&
               physics->Raycast(origin, direction, maxDistance, outDistance, outNormal, ignoredBody);
    }

    std::vector<NS::AABB> OverlapBoxCollision(const IUseCollision* user, const NS::AABB& region)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const NS::Phys::PhysicsScene* physics = user->GetPhysicsScene();
        if (physics == nullptr)
        {
            return {};
        }
        return physics->OverlapBox(region);
    }
} // namespace NS::Obj
