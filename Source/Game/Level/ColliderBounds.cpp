#include "Game/Level/ColliderBounds.h"

#include "Runtime/Core/AABB.h"
#include "Runtime/Object/Components/BoxCollider.h"
#include "Runtime/Object/Components/SphereCollider.h"
#include "Runtime/Object/GameObject.h"

namespace NS::Game::Level
{
    bool TryGetColliderBounds(const NS::Obj::GameObject& object, NS::Core::AABB& outBounds) noexcept
    {
        // 箱と球の両方を持つ配置物は無いので、どちらを先に見ても結果は変わらない
        if (const NS::Obj::BoxCollider* box = object.FindComponent<NS::Obj::BoxCollider>())
        {
            outBounds = box->WorldAABB();
            return true;
        }
        if (const NS::Obj::SphereCollider* sphere = object.FindComponent<NS::Obj::SphereCollider>())
        {
            outBounds = sphere->WorldAABB();
            return true;
        }
        return false;
    }
} // namespace NS::Game::Level
