#include "Game/Level/ImpactResolverComponent.h"

#include "Game/Level/BreakableComponent.h"
#include "Game/Level/LaunchedBodyComponent.h"
#include "Game/Level/MomentumComponent.h"
#include "Runtime/Core/Clock.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/BoxColliderComponent.h"
#include "Runtime/Object/Components/CameraBrainComponent.h"
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

        // 伸びから元の形へ戻す歩数。反発の滞空 0.3 秒の前半で戻し切り、着地の前に形を確定させる
        constexpr int k_StretchRecoverSteps = 6;
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
            {
                ReleaseHitStop();
                return;
            }
            ApplyFreezeVibration();
            return;
        }

        if (m_freezePendingSteps > 0)
        {
            BeginFreeze(m_freezePendingSteps);
            m_freezePendingSteps = 0;
            return;
        }

        if (m_recoverRemaining > 0)
            RecoverScale();

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
        const float massFactor = mass / (mass + 1.0f);
        float rebound = m_reboundSpeed * ratio * massFactor;
        rebound = NS::Core::Clamp(rebound, 0.0f, k_MaxReboundSpeed);

        // 質量で割ると重い物ほど飛ばない
        float launch = m_launchSpeed * ratio / mass;
        launch = NS::Core::Clamp(launch, 0.0f, k_MaxLaunchSpeed);

        m_pendingSelfVelocity = NS::Core::Vector3{awayX * rebound, m_reboundUpSpeed, awayZ * rebound};
        m_pendingLaunchVelocity = NS::Core::Vector3{-awayX * launch, launch * m_launchUpScale, -awayZ * launch};
        m_pendingTargetId = hit->Owner()->Id();
        m_pendingTargetHome = hit->Owner()->Root().Position();
        m_pendingImpactDir = NS::Core::Vector3{-awayX, 0.0f, -awayZ};
        // 反発の質量因子の残り。動きは軽い側が受け取るので、重い物ほど揺れない
        m_pendingShakeAmplitude = m_shakeAmplitude / (1.0f + mass);
        m_pendingShakeStrength = m_cameraShakeScale * ratio * massFactor;
        m_didRebound = true;
        NS_LOG_INFO(Game, "衝突: 質量 {} 耐久 {} 返り {} 押し飛ばし {}", mass, hit->Toughness(), rebound, launch);

        const int stopSteps = ComputeHitStopSteps(ratio, mass);
        if (stopSteps <= 0)
        {
            ReleaseHitStop();
            return;
        }

        // 凍結は次の歩から。この歩は移動が最後の 1 歩を走り、自機が箱へ触れてから止まる
        m_freezePendingSteps = stopSteps;
    }

    void ImpactResolverComponent::BeginFreeze(int stopSteps)
    {
        // 自機を寝かせて凍らせる。World::UpdateObjects は active をその場で見るので同じ歩から効く
        m_hitStopRemaining = stopSteps;
        m_hitStopTotal = stopSteps;
        m_movement->SetActive(false);
        NS_LOG_INFO(Game, "ヒットストップ: {} 歩", stopSteps);

        // 潰れは反発の前半。進行方向の厚みを潰し、行き場を失った体積を縦へ逃がす
        // 戻りの最中に次の衝突が来たら、控え済みの元の形をそのまま使い続ける
        if (m_recoverRemaining == 0)
            m_scaleHome = RootTransform().Scale();
        m_recoverRemaining = 0;
        m_scaleHeld = true;
        RootTransform().SetScale(ScaledAlongImpact(m_squashThickness, m_squashHeight));

        NS::Object::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
            return;

        // 力が伝わった瞬間の絵。凍結の頭で相手を発射方向へ食い込ませて止める。当たりは動かさない
        if (NS::Object::GameObject* target = scene->World().FindByObjectId(m_pendingTargetId))
            target->Root().SetPosition(m_pendingTargetHome + m_pendingImpactDir * m_pushInDistance);

        if (NS::Object::CameraBrainComponent* brain = scene->CameraBrain())
            brain->StartShake(m_pendingShakeStrength, stopSteps);
    }

    void ImpactResolverComponent::ReleaseHitStop()
    {
        m_movement->SetActive(true);
        m_movement->SetVelocity(m_pendingSelfVelocity);
        if (m_scaleHeld)
        {
            // 解放の伸びが反発そのもの。弾かれる軸は進行の軸と同じで、高さは戻して横だけ伸ばす
            m_stretchScale = ScaledAlongImpact(m_stretchAlong, 1.0f);
            RootTransform().SetScale(m_stretchScale);
            m_recoverRemaining = k_StretchRecoverSteps;
            m_scaleHeld = false;
        }
        // 猶予はここから数え始める。止まっている間に数えると、操作できないまま猶予が減る
        m_momentum->BeginGrace();

        NS::Object::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
            return;
        // 相手は id で引き直す。止まっている数歩の間に消されていたら発射だけ諦める
        NS::Object::GameObject* target = scene->World().FindByObjectId(m_pendingTargetId);
        if (target == nullptr)
            return;
        // 食い込みと振動は絵だけ。発射の起点がずれないよう元位置へ厳密に戻してから発射する
        target->Root().SetPosition(m_pendingTargetHome);
        // 積み忘れた配置物でも押し飛ばせるよう、無ければその場で足す
        auto* body = target->FindComponent<LaunchedBodyComponent>();
        if (body == nullptr)
            body = target->AddComponent<LaunchedBodyComponent>();
        body->Launch(m_pendingLaunchVelocity);
    }

    void ImpactResolverComponent::ApplyFreezeVibration()
    {
        NS::Object::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
            return;
        NS::Object::GameObject* target = scene->World().FindByObjectId(m_pendingTargetId);
        if (target == nullptr || m_hitStopTotal <= 0)
            return;

        // 歩数の偶奇で往復し、残り歩数で減衰する。乱数を使わないので同じ入力は同じ絵になる
        const float sign = 1.0f - 2.0f * static_cast<float>(m_hitStopRemaining % 2);
        const float decay = static_cast<float>(m_hitStopRemaining) / static_cast<float>(m_hitStopTotal);
        const float along = m_pushInDistance + m_pendingShakeAmplitude * sign * decay;
        target->Root().SetPosition(m_pendingTargetHome + m_pendingImpactDir * along);
    }

    void ImpactResolverComponent::RecoverScale()
    {
        --m_recoverRemaining;
        if (m_recoverRemaining <= 0)
        {
            // 補間の残差を残さない。控えた元の値をそのまま書いて形を確定させる
            RootTransform().SetScale(m_scaleHome);
            return;
        }
        const float t = static_cast<float>(m_recoverRemaining) / static_cast<float>(k_StretchRecoverSteps);
        RootTransform().SetScale(m_scaleHome + (m_stretchScale - m_scaleHome) * t);
    }

    NS::Core::Vector3 ImpactResolverComponent::ScaledAlongImpact(float along, float height) const noexcept
    {
        // 衝突は水平でしか起きない。進行の軸成分の 2 乗で倍率を混ぜ、軸に載った衝突では素の倍率になる
        const float dx2 = m_pendingImpactDir.x * m_pendingImpactDir.x;
        const float dz2 = m_pendingImpactDir.z * m_pendingImpactDir.z;
        return NS::Core::Vector3{m_scaleHome.x * (1.0f + (along - 1.0f) * dx2),
                                 m_scaleHome.y * height,
                                 m_scaleHome.z * (1.0f + (along - 1.0f) * dz2)};
    }

    int ImpactResolverComponent::ComputeHitStopSteps(float ratio, float mass) const noexcept
    {
        // 質量差をそのまま歩数に出すと停止が伸びすぎるので平方根で圧縮する
        const float raw = m_hitStopBaseSeconds * ratio * std::sqrt(mass) / NS::Core::FrameTimer::FixedDelta();
        if (!std::isfinite(raw))
            return 0;
        const int steps = static_cast<int>(std::lround(raw));
        return NS::Core::Clamp(steps, 0, k_MaxHitStopSteps);
    }

    NS_CLASS(ImpactResolverComponent)
} // namespace NS::Game::Level
