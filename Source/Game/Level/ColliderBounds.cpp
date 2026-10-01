#include "Game/Level/ColliderBounds.h"

#include "Runtime/Core/AABB.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/BoxCollider.h"
#include "Runtime/Object/Components/SphereCollider.h"

namespace NS::Game::Level
{
    bool TryGetColliderBounds(const NS::Obj::Actor& object, NS::Core::AABB& outBounds) noexcept
    {
        // 箱と球の両方を持つ配置物は無いので、どちらを先に見ても結果は変わらない
        if (const NS::Obj::BoxCollider* box = NS::Obj::ComponentCast<NS::Obj::BoxCollider>(object.CollisionPart()))
        {
            outBounds = box->WorldAABB();
            return true;
        }
        if (const NS::Obj::SphereCollider* sphere =
                NS::Obj::ComponentCast<NS::Obj::SphereCollider>(object.CollisionPart()))
        {
            outBounds = sphere->WorldAABB();
            return true;
        }
        return false;
    }
} // namespace NS::Game::Level
