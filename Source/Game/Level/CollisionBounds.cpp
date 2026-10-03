#include "Game/Level/CollisionBounds.h"

#include "Runtime/Core/AABB.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/BoxCollision.h"
#include "Runtime/Object/Components/SphereCollision.h"

namespace NS::Game::Level
{
    bool TryGetCollisionBounds(const NS::Obj::Actor& object, NS::Core::AABB& outBounds) noexcept
    {
        // 箱と球の両方を持つ配置物は無いので、どちらを先に見ても結果は変わらない
        if (const NS::Obj::BoxCollision* box = NS::Obj::ComponentCast<NS::Obj::BoxCollision>(object.CollisionPart()))
        {
            outBounds = box->WorldAABB();
            return true;
        }
        if (const NS::Obj::SphereCollision* sphere =
                NS::Obj::ComponentCast<NS::Obj::SphereCollision>(object.CollisionPart()))
        {
            outBounds = sphere->WorldAABB();
            return true;
        }
        return false;
    }
} // namespace NS::Game::Level
