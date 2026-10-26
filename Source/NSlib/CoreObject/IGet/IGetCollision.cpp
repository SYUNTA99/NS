#include "NSlib/CoreObject/IGet/IGetCollision.h"
#include "NSlib/Core/Assert.h"

#include "NSlib/CoreObject/ClassObjs/Collision.h"
#include "NSlib/Phys/PhysicsScene.h"

namespace NS
{
    bool RaycastCollision(const IGetCollision* user,
                          const Vector3& origin,
                          const Vector3& direction,
                          float maxDistance,
                          float& outDistance)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const Phys::PhysicsScene* physics = user->GetPhysicsScene();
        return physics != nullptr && physics->Raycast(origin, direction, maxDistance, outDistance);
    }

    bool RaycastCollision(const IGetCollision* user,
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

    std::vector<AABB> OverlapBoxCollision(const IGetCollision* user, const AABB& region)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const Phys::PhysicsScene* physics = user->GetPhysicsScene();
        if (physics == nullptr)
        {
            return {};
        }
        return physics->OverlapBox(region);
    }

    void InitCollision(const IGetCollision* user, Collision* collision, const Matrix& world)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, collision != nullptr, "InitCollision: 当たりが null");
        collision->SyncToPhysics(world, user);
        collision->RemoveFromPhysics(user);
    }

    void EnableCollision(const IGetCollision* user, Collision* collision, const Matrix& world)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, collision != nullptr, "EnableCollision: 当たりが null");
        collision->ReturnToPhysics(world, user);
    }

    void DisableCollision(const IGetCollision* user, Collision* collision)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, collision != nullptr, "DisableCollision: 当たりが null");
        collision->RemoveFromPhysics(user);
    }

    void EndCollision(const IGetCollision* user, Collision* collision)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, collision != nullptr, "EndCollision: 当たりが null");
        collision->Unregister(user);
    }
} // namespace NS
