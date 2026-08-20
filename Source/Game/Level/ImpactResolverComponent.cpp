#include "Game/Level/ImpactResolverComponent.h"

#include "Game/Level/BreakableComponent.h"
#include "Game/Level/MomentumComponent.h"
#include "Runtime/Core/Clock.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/BoxColliderComponent.h"
#include "Runtime/Object/Components/CharacterMovementComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/World.h"
#include "Runtime/Physics/Capsule.h"

#include <cmath>

namespace NS::Game::Level
{
    namespace
    {
        // 向きと言える下限 0.0001 の 2 乗。完全に重なると正規化が 0 除算になり、NaN が速度へ流れる
        constexpr float k_MinDirectionLengthSq = 1e-8f;
    } // namespace

    // MomentumComponent の 50 より後。先に走ると BeginGrace した猶予がその固定ステップのうちに解ける
    // CharacterMovementComponent の 200 より前。書き込んだ速度が同じ固定ステップの移動に乗る
    ImpactResolverComponent::ImpactResolverComponent() noexcept
        : NS::Object::Component(NS::Object::TickPriority::Update - 100)
    {}

    void ImpactResolverComponent::OnStart()
    {
        m_movement = Owner()->FindComponent<NS::Object::CharacterMovementComponent>();
        m_momentum = Owner()->FindComponent<MomentumComponent>();
    }

    BreakableComponent* ImpactResolverComponent::FindOverlapped() const
    {
        NS::Object::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
            return nullptr;

        const NS::Core::Vector3 position = Owner()->Root().Position();
        const NS::Core::Vector3 velocity = m_movement->Velocity();
        const float dt = NS::Core::FrameTimer::FixedDelta();

        // この固定ステップで進んだ先で見る。今の位置だけでは手前で止められて重ならず、反発が起きない
        NS::Physics::Capsule capsule{};
        capsule.center =
            NS::Core::Vector3{position.x + velocity.x * dt, position.y + velocity.y * dt, position.z + velocity.z * dt};
        capsule.radius = m_movement->CapsuleRadius();
        capsule.halfHeight = m_movement->CapsuleHalfHeight();

        // TODO: 壊せる物を総当たりで見ている。数十個までを想定。増えたら格子で絞る
        BreakableComponent* nearest = nullptr;
        float nearestDistanceSq = 0.0f;
        scene->World().ForEachComponent<BreakableComponent>([&](BreakableComponent& breakable) {
            if (!breakable.IsActive())
                return;

            // トリガの箱は通り抜ける体積なのでぶつかる相手にならない
            auto* box = breakable.Owner()->FindComponent<NS::Object::BoxColliderComponent>();
            if (box == nullptr || box->IsTrigger())
                return;

            const NS::Core::AABB bounds = box->WorldAABB();
            if (!NS::Physics::IntersectsCapsuleAABB(capsule, bounds))
                return;

            const float dx = bounds.Center.x - position.x;
            const float dy = bounds.Center.y - position.y;
            const float dz = bounds.Center.z - position.z;
            const float distanceSq = dx * dx + dy * dy + dz * dz;
            if (nearest == nullptr || distanceSq < nearestDistanceSq)
            {
                nearest = &breakable;
                nearestDistanceSq = distanceSq;
            }
        });
        return nearest;
    }

    void ImpactResolverComponent::OnUpdate()
    {
        m_didRebound = false;
        if (m_movement == nullptr || m_momentum == nullptr)
            return;

        BreakableComponent* hit = FindOverlapped();
        if (hit == nullptr)
            return;

        auto* box = hit->Owner()->FindComponent<NS::Object::BoxColliderComponent>();
        if (box == nullptr)
            return;

        const NS::Core::AABB bounds = box->WorldAABB();
        const NS::Core::Vector3 position = Owner()->Root().Position();
        const NS::Core::Vector3 velocity = m_movement->Velocity();

        // 弾かれる向きは箱と自機の並びで決まる。水平だけを見て、上向きは別の値で足す
        float awayX = position.x - bounds.Center.x;
        float awayZ = position.z - bounds.Center.z;
        float lengthSq = awayX * awayX + awayZ * awayZ;
        // 箱の中心へ重なると向きが決まらない。進んできた向きの逆へ弾く
        if (lengthSq < k_MinDirectionLengthSq)
        {
            awayX = -velocity.x;
            awayZ = -velocity.z;
            lengthSq = awayX * awayX + awayZ * awayZ;
            if (lengthSq < k_MinDirectionLengthSq)
                return;
        }
        const float invLength = 1.0f / std::sqrt(lengthSq);
        awayX *= invLength;
        awayZ *= invLength;

        // 箱へ向かっている歩だけ弾く。離れていく間も弾くと、重なりが解けるまで毎歩掛かり直す
        if (velocity.x * awayX + velocity.z * awayZ >= 0.0f)
            return;

        m_movement->SetVelocity(NS::Core::Vector3{awayX * m_reboundSpeed, m_reboundUpSpeed, awayZ * m_reboundSpeed});
        m_momentum->BeginGrace();
        m_didRebound = true;
        NS_LOG_INFO(Game, "反発: 質量 {} 耐久 {}", hit->Mass(), hit->Toughness());
    }

    NS_CLASS(ImpactResolverComponent)
} // namespace NS::Game::Level
