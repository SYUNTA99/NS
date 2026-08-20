#include "Game/Level/ImpactResolverComponent.h"

#include "Game/Level/BreakableComponent.h"
#include "Game/Level/LaunchedBodyComponent.h"
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

        // 質量の下限 0.01 で割ると初速が 100 倍まで跳ねる。画面の外へ消える前に頭打ちにする
        constexpr float k_MaxLaunchSpeed = 60.0f;

        // 反発の頭打ち。最高ダッシュ 16 の 1.5 倍。質量因子は 1 未満に飽和するので通常の遊びでは届かず、
        // 基準初速に桁違いの値を入れた時に操作の成立を守る
        constexpr float k_MaxReboundSpeed = 24.0f;

        // 止める歩数の上限 12 歩 (0.2 秒)。これより長い停止は衝突の重さではなく処理落ちに見える
        constexpr int k_MaxHitStopSteps = 12;
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

        // 止まっている間は新しい衝突を見ない。凍った自機は重なったままなので、見ると毎歩検知し直す
        if (m_hitStopRemaining > 0)
        {
            --m_hitStopRemaining;
            if (m_hitStopRemaining == 0)
                ReleaseHitStop();
            return;
        }

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

        // 当たった瞬間の水平速度を通常速度で割った比が勢いの強さ。反発も発射もこの 1 つの比から作る
        const float impactSpeed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
        const float normalSpeed = m_momentum->SpeedForLevel(MomentumLevel::Normal);
        float ratio = 0.0f;
        if (normalSpeed > 0.0f)
            ratio = impactSpeed / normalSpeed;
        const float mass = hit->Mass();

        // 質量因子 mass/(mass+1) は質量が大きいほど 1 へ寄る。重い物は入った速さがほぼそのまま返り、
        // 軽い物は勢いを持っていくのでほとんど返らない
        float rebound = m_reboundSpeed * ratio * (mass / (mass + 1.0f));
        rebound = NS::Core::Clamp(rebound, 0.0f, k_MaxReboundSpeed);

        // 質量で割ると重い物ほど飛ばない
        float launch = m_launchSpeed * ratio / mass;
        launch = NS::Core::Clamp(launch, 0.0f, k_MaxLaunchSpeed);

        m_pendingSelfVelocity = NS::Core::Vector3{awayX * rebound, m_reboundUpSpeed, awayZ * rebound};
        m_pendingLaunchVelocity = NS::Core::Vector3{-awayX * launch, launch * m_launchUpScale, -awayZ * launch};
        m_pendingTargetId = hit->Owner()->Id();
        m_didRebound = true;
        NS_LOG_INFO(Game, "衝突: 質量 {} 耐久 {} 返り {} 押し飛ばし {}", mass, hit->Toughness(), rebound, launch);

        const int stopSteps = ComputeHitStopSteps(ratio, mass);
        if (stopSteps <= 0)
        {
            ReleaseHitStop();
            return;
        }

        // 自機を寝かせて凍らせる。World::UpdateObjects は active をその場で見るので同じ歩から効く
        m_hitStopRemaining = stopSteps;
        m_movement->SetActive(false);
        NS_LOG_INFO(Game, "ヒットストップ: {} 歩", stopSteps);
    }

    void ImpactResolverComponent::ReleaseHitStop()
    {
        m_movement->SetActive(true);
        m_movement->SetVelocity(m_pendingSelfVelocity);
        // 猶予はここから数え始める。止まっている間に数えると、操作できないまま猶予が減る
        m_momentum->BeginGrace();

        NS::Object::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
            return;
        // 相手は id で引き直す。止まっている数歩の間に消されていたら発射だけ諦める
        NS::Object::GameObject* target = scene->World().FindByObjectId(m_pendingTargetId);
        if (target == nullptr)
            return;
        // 積み忘れた配置物でも押し飛ばせるよう、無ければその場で足す
        auto* body = target->FindComponent<LaunchedBodyComponent>();
        if (body == nullptr)
            body = target->AddComponent<LaunchedBodyComponent>();
        body->Launch(m_pendingLaunchVelocity);
    }

    int ImpactResolverComponent::ComputeHitStopSteps(float ratio, float mass) const noexcept
    {
        // 質量は平方根で圧縮する。質量の幅は 100 倍あるが、停止は 1 秒の何分の一かに収めたい
        const float raw = m_hitStopScale * ratio * std::sqrt(mass);
        if (!std::isfinite(raw))
            return 0;
        const int steps = static_cast<int>(std::lround(raw));
        return NS::Core::Clamp(steps, 0, k_MaxHitStopSteps);
    }

    NS_CLASS(ImpactResolverComponent)
} // namespace NS::Game::Level
