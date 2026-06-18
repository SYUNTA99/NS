#include "Framework/Scene/Components/StaticColliderComponent.h"

#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Transform.h"

namespace NS::Scene
{
    namespace
    {
        [[nodiscard]] NS::Math::Vector3 ClampNonNegative(const NS::Math::Vector3& v) noexcept
        {
            return NS::Math::Vector3{
                v.x < 0.0f ? 0.0f : v.x,
                v.y < 0.0f ? 0.0f : v.y,
                v.z < 0.0f ? 0.0f : v.z,
            };
        }
    } // namespace

    StaticColliderComponent::StaticColliderComponent() noexcept {}

    StaticColliderComponent::StaticColliderComponent(const NS::Math::Vector3& halfExtents) noexcept
        : m_halfExtents(ClampNonNegative(halfExtents))
    {}

    void StaticColliderComponent::SetHalfExtents(const NS::Math::Vector3& halfExtents) noexcept
    {
        m_halfExtents = ClampNonNegative(halfExtents);
    }

    NS::Math::Vector3 StaticColliderComponent::HalfExtents() const noexcept
    {
        return m_halfExtents;
    }

    NS::Math::AABB StaticColliderComponent::WorldAABB() const noexcept
    {
        const NS::Math::AABB local(NS::Math::Vector3{0.0f, 0.0f, 0.0f}, m_halfExtents);
        const GameObject* owner = Owner();
        if (owner == nullptr)
            return local;

        // owner の world 変換 (平行移動 / 回転 / スケール) を box に適用する。 grid (scale=1・整数位置) では
        // 結果が従来と一致し、 自由配置物は scale / 回転が当たりへ反映される。 回転時は内包する軸並行 AABB になる
        NS::Math::AABB world;
        local.Transform(world, owner->Root().WorldMatrix());
        return world;
    }
} // namespace NS::Scene
