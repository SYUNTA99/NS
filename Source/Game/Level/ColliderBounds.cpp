#include "Game/Level/ColliderBounds.h"

#include "Runtime/Object/Components/BoxColliderComponent.h"
#include "Runtime/Object/Components/SphereColliderComponent.h"
#include "Runtime/Object/GameObject.h"

namespace NS::Game::Level
{
    bool TryGetColliderBounds(const NS::Object::GameObject& object, NS::Core::AABB& outBounds) noexcept
    {
        // 箱と球の両方を持つ配置物は無いので、どちらを先に見ても結果は変わらない
        if (const auto* box = object.FindComponent<NS::Object::BoxColliderComponent>())
        {
            outBounds = box->WorldAABB();
            return true;
        }
        if (const auto* sphere = object.FindComponent<NS::Object::SphereColliderComponent>())
        {
            outBounds = sphere->WorldAABB();
            return true;
        }
        return false;
    }
} // namespace NS::Game::Level
