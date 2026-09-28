#include "Game/Level/ColliderBounds.h"

#include "Runtime/Core/AABB.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Components/BoxCollider.h"
#include "Runtime/Object/Components/Collider.h"
#include "Runtime/Object/Components/SphereCollider.h"
#include "Runtime/Object/GameObject.h"

namespace NS::Game::Level
{
    BodyColliderSearch FindBodyCollider(const NS::Obj::GameObject& object) noexcept
    {
        BodyColliderSearch search;
        const NS::Obj::Collider* found = nullptr;
        for (const NS::Obj::Component* component : object.Components())
        {
            const NS::Obj::Collider* collider = NS::Obj::ComponentCast<NS::Obj::Collider>(component);
            if (collider == nullptr || collider->IsTrigger() || collider->IsExcludedFromPhysics())
            {
                continue;
            }
            found = collider;
            ++search.count;
        }
        if (search.count == 1)
        {
            search.body = found;
        }
        return search;
    }

    bool TryGetColliderBounds(const NS::Obj::GameObject& object, NS::Core::AABB& outBounds) noexcept
    {
        const NS::Obj::Collider* body = FindBodyCollider(object).body;
        if (const NS::Obj::BoxCollider* box = NS::Obj::ComponentCast<NS::Obj::BoxCollider>(body))
        {
            outBounds = box->WorldAABB();
            return true;
        }
        if (const NS::Obj::SphereCollider* sphere = NS::Obj::ComponentCast<NS::Obj::SphereCollider>(body))
        {
            outBounds = sphere->WorldAABB();
            return true;
        }
        return false;
    }
} // namespace NS::Game::Level
