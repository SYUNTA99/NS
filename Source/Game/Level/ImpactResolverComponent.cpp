#include "Game/Level/ImpactResolverComponent.h"

#include "Game/Level/BreakableComponent.h"
#include "Game/Level/ColliderBounds.h"
#include "Game/Level/CollisionInputComponent.h"
#include "Game/Level/ImpactMarkComponent.h"
#include "Game/Level/LaunchedBodyComponent.h"
#include "Game/Level/MomentumComponent.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Core/Clock.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Graphics/RenderContext.h"
#include "Runtime/Graphics/Renderer.h"
#include "Runtime/Object/Components/BoxColliderComponent.h"
#include "Runtime/Object/Components/CameraBrainComponent.h"
#include "Runtime/Object/Components/ColliderComponent.h"
#include "Runtime/Object/Components/MeshRendererComponent.h"
#include "Runtime/Object/Components/ShadowComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/World.h"
#include "Runtime/Physics/Capsule.h"
#include "Runtime/Physics/PhysicsWorld.h"

#include <cmath>
#include <memory>
#include <utility>

namespace NS::Game::Level
{
    namespace
    {
        // 質量の下限 0.01 で割ると初速が 100 倍まで跳ねる。画面の外へ消える前に頭打ちにする
        constexpr float k_MaxLaunchSpeed = 120.0f;

        // 反発の頭打ち。最高ダッシュ 16 の 1.5 倍。素の係数では質量因子が 1 未満に飽和して届かず、
        // 入力係数と重い相手が重なった時と、基準初速に桁違いの値を入れた時に操作の成立を守る
        constexpr float k_MaxReboundSpeed = 24.0f;

        // 止める歩数の上限 12 歩 (0.2 秒)。これより長い停止は衝突の重さではなく処理落ちに見える
        constexpr int k_MaxHitStopSteps = 12;

        // 伸びから元の形へ戻す歩数。反発の滞空 0.3 秒の前半で戻し切り、着地の前に形を確定させる
        constexpr int k_StretchRecoverSteps = 6;

        // 壊れた物の見た目の色。破片が出るまでの仮の差し替え
        constexpr NS::Core::Vector3 k_BrokenBaseColor{0.25f, 0.22f, 0.20f};

        // 跡の床探しで真下を見る上限。これより下に床が無ければ跡を出さない
        constexpr float k_MarkProbeDistance = 64.0f;

        // 床の上面から跡を浮かせる高さ。面がぴったり重なるとちらつく
        constexpr float k_MarkFloorOffset = 0.02f;

        // 破片 1 個の描画スケール。壊れた物より明確に小さくして、数で壊れた量を見せる
        constexpr float k_DebrisScale = 0.25f;

        // 破片の色。壊れた本体より少し明るくして欠けた中身に見せる
        constexpr NS::Core::Vector3 k_DebrisBaseColor{0.35f, 0.32f, 0.30f};

        // ピークで当てた時だけの白フラッシュ。音が無い間の唯一の瞬間報酬なので、端で当てた時と見間違えない強さにする
        // 0.5 は一瞬白と分かる濃さ。1.0 だと衝突の絵 (食い込みと潰れ) が隠れる
        constexpr float k_PeakFlashAlpha = 0.5f;
        // 8 歩 (約 0.13 秒)。ヒットストップの尺に収まる一瞬で、走り出しの視界に白を残さない
        constexpr int k_PeakFlashSteps = 8;
    } // namespace

    // MomentumComponent (-150) より後。先に走ると BeginGrace した猶予がその固定ステップのうちに解ける
    // PlayerComponent の 200 より前。書き込んだ速度が同じ固定ステップの移動に乗る
    ImpactResolverComponent::ImpactResolverComponent() noexcept
        : NS::Object::OverlayRendererComponent(NS::Object::TickPriority::Update - 100)
    {}

    void ImpactResolverComponent::OnStart()
    {
        m_movement = Owner()->FindComponent<NS::Game::Player::PlayerComponent>();
        m_momentum = Owner()->FindComponent<MomentumComponent>();
        // 無ければ null のまま。null は常に素と同じ経路なので、ボタン未搭載の配置物は従来のまま動く
        m_collisionInput = Owner()->FindComponent<CollisionInputComponent>();
    }

    BreakableComponent* ImpactResolverComponent::FindOverlapped() const
    {
        NS::Object::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
            return nullptr;

        const NS::Core::Vector3 position = Owner()->Root().Position();
        const NS::Core::Vector3 velocity = m_movement->BodySlamVelocity();
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
            const auto* box = breakable.Owner()->FindComponent<NS::Object::BoxColliderComponent>();
            if (box != nullptr && box->IsTrigger())
                return;

            NS::Core::AABB bounds{};
            if (!TryGetColliderBounds(*breakable.Owner(), bounds))
                return;

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
        m_didBreak = false;
        // フラッシュの減衰は早期 return より前に置く。凍結中の歩もここまでは来るので、止まっている間も白が薄れる
        if (m_peakFlashRemaining > 0)
            --m_peakFlashRemaining;
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

        // 押していない接触は物理の停止だけで済ませるため、体当たり中でない歩は裁定しない
        if (!m_movement->IsBodySlamming())
            return;

        BreakableComponent* hit = FindOverlapped();
        if (hit == nullptr)
            return;

        NS::Core::AABB bounds{};
        if (!TryGetColliderBounds(*hit->Owner(), bounds))
            return;

        const NS::Core::Vector3 position = Owner()->Root().Position();
        // 箱へ押し付けられた歩は実速度が 0 に潰されるため、突進の狙いの速度で向きと勢いを決める
        const NS::Core::Vector3 velocity = m_movement->BodySlamVelocity();

        // 弾かれる向きは箱と自機の並びで決まる。水平だけを見て、上向きは別の値で足す
        float awayX = position.x - bounds.Center.x;
        float awayZ = position.z - bounds.Center.z;
        float lengthSq = awayX * awayX + awayZ * awayZ;
        // 箱の中心へ重なると向きが決まらない。進んできた向きの逆へ弾く
        if (lengthSq < NS::Core::k_Epsilon * NS::Core::k_Epsilon)
        {
            awayX = -velocity.x;
            awayZ = -velocity.z;
            lengthSq = awayX * awayX + awayZ * awayZ;
            if (lengthSq < NS::Core::k_Epsilon * NS::Core::k_Epsilon)
                return;
        }
        const float invLength = 1.0f / std::sqrt(lengthSq);
        awayX *= invLength;
        awayZ *= invLength;

        // 箱へ向かっている歩だけ弾く。離れていく間も弾くと、重なりが解けるまで毎歩掛かり直す
        if (velocity.x * awayX + velocity.z * awayZ >= 0.0f)
            return;

        // 比は勢いの段から作る。実速度から作ると、手を放して減速し始めた歩の発動だけ威力が落ちる
        const float normalSpeed = m_momentum->SpeedForLevel(MomentumLevel::Normal);
        // 通常速度が 0 の壊れたデータでは比が作れない。1.0 は通常の段で当てたのと同じ
        float ratio = 1.0f;
        if (normalSpeed > 0.0f)
            ratio = m_momentum->SpeedForLevel(m_momentum->Level()) / normalSpeed;
        const float mass = hit->Mass();
        const float massFactor = mass / (mass + 1.0f);

        // ボタン未搭載 (null) は係数 1.0 の素通し。1.0f の乗算は IEEE で恒等なので、係数を掛けない式とビット同値
        const float charge01 = m_movement->BodySlamCharge01();
        const float progress01 = m_movement->BodySlamProgress01();
        float chargeFactor = 1.0f;
        float positionFactor = 1.0f;
        bool peak = false;
        if (m_collisionInput != nullptr)
        {
            chargeFactor = m_collisionInput->ChargeFactorFor(charge01);
            positionFactor = m_collisionInput->PositionFactorFor(progress01);
            peak = m_collisionInput->IsPeak(positionFactor);
        }
        // 最終威力 = 比 × チャージ倍率 × 突進位置係数。破壊の物差しだけでなく反発・発射・揺れも威力で作る
        const float power = ratio * chargeFactor * positionFactor;
        m_lastCharge01 = charge01;
        m_lastPositionFactor = positionFactor;
        m_lastPower = power;
        m_wasPeakImpact = peak;
        float hitStopScale = 1.0f;
        if (peak)
        {
            m_peakFlashRemaining = k_PeakFlashSteps;
            hitStopScale = m_peakHitStopScale;
        }
        NS_LOG_INFO(Game,
                    "威力の内訳: 比 {} × 溜め {} × 位置 {} = {} 溜め量 {} 突進 {}",
                    ratio,
                    chargeFactor,
                    positionFactor,
                    power,
                    charge01,
                    progress01);

        // 明けた歩の反発と貫通速度を Locomotion に乗せるため、凍結より先に突進を打ち切る
        m_movement->CancelBodySlam();

        m_pendingTargetId = hit->Owner()->Id();
        m_pendingTargetHome = hit->Owner()->Root().Position();
        m_pendingImpactDir = NS::Core::Vector3{-awayX, 0.0f, -awayZ};
        // 反発の質量因子の残り。動きは軽い側が受け取るので、重い物ほど揺れない
        m_pendingShakeAmplitude = m_shakeAmplitude / (1.0f + mass);
        m_pendingShakeStrength = m_cameraShakeScale * power * massFactor;

        // 最高ダッシュ限定の破壊条件は外した。耐久 ≤ 最終威力で壊れないと、
        // ダッシュ + ピークが最高ダッシュ + 素を上回る逆転が成立しない
        // 欄を下ろしている間は耐久を見ない。壊れる相手も押し飛ばしと反発へ回る
        int stopSteps = 0;
        if (m_breakEnabled && hit->Toughness() <= power)
        {
            m_pendingBreak = true;
            // 向きを保ったまま減速する。倍率は相手の質量に依らない
            m_pendingSelfVelocity = velocity * m_breakSpeedScale;
            m_didBreak = true;
            NS_LOG_INFO(Game, "貫通: 耐久 {} 威力 {} ピーク {}", hit->Toughness(), power, peak);
            stopSteps = SecondsToSteps(m_breakStopSeconds * hitStopScale);
        }
        else
        {
            m_pendingBreak = false;
            // 質量因子 mass/(mass+1) は質量が大きいほど 1 へ寄る。重い物は入った速さがほぼそのまま返り、
            // 軽い物は勢いを持っていくのでほとんど返らない
            float rebound = m_reboundSpeed * power * massFactor;
            rebound = NS::Core::Clamp(rebound, 0.0f, k_MaxReboundSpeed);

            // 指数の範囲は 0〜1。負は重い物ほど飛ぶ逆転、1 超えは重い側がまったく動かない
            float massExponent = m_launchMassExponent;
            if (!std::isfinite(massExponent))
                massExponent = 1.0f;
            massExponent = NS::Core::Clamp(massExponent, 0.0f, 1.0f);

            // 質量で割ると重い物ほど飛ばない
            float launch = m_launchSpeed * power / std::pow(mass, massExponent);
            launch = NS::Core::Clamp(launch, 0.0f, k_MaxLaunchSpeed);

            m_pendingSelfVelocity = NS::Core::Vector3{awayX * rebound, m_reboundUpSpeed, awayZ * rebound};
            m_pendingLaunchVelocity = NS::Core::Vector3{-awayX * launch, launch * m_launchUpScale, -awayZ * launch};
            m_didRebound = true;
            NS_LOG_INFO(Game,
                        "衝突: 質量 {} 耐久 {} 返り {} 押し飛ばし {} ピーク {}",
                        mass,
                        hit->Toughness(),
                        rebound,
                        launch,
                        peak);
            stopSteps = ComputeHitStopSteps(power, mass, hitStopScale);
        }

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

        // 潰れは反発の前半。進行方向の厚みを潰し、代わりに高さを伸ばす
        // 戻りの最中に次の衝突が来たら、控え済みの元の形をそのまま使い続ける
        if (m_recoverRemaining == 0)
            m_scaleHome = RootTransform().Scale();
        m_recoverRemaining = 0;
        m_scaleHeld = true;
        // 貫通は押し勝っている側なので潰さない。潰れは押し返されている反発だけの絵
        if (!m_pendingBreak)
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

    void ImpactResolverComponent::OnEndPlay()
    {
        // 凍結の途中で裁定が外れても、移動が止まったまま残らないようにする
        if (m_movement != nullptr)
            m_movement->SetActive(true);
        m_peakFlashRemaining = 0;
    }

    void ImpactResolverComponent::OnRenderOverlay(const NS::Graphics::RenderContext& ctx)
    {
        if (m_peakFlashRemaining <= 0)
            return;
        // ScreenFadeComponent は黒の固定色と暗転の段階機械で、白の瞬間減衰には流用できないためここで直接描く
        const float decay = static_cast<float>(m_peakFlashRemaining) / static_cast<float>(k_PeakFlashSteps);
        ctx.renderer->DrawFullscreenColor(NS::Core::Color{1.0f, 1.0f, 1.0f, k_PeakFlashAlpha * decay});
    }

    void ImpactResolverComponent::BreakTarget(NS::Object::GameObject& target)
    {
        // 壊れた物は世界から消さない。更新の最中に消すと集めた並びに解放済みの位置が残る
        // 印と当たりを寝かせて探索と固形から外し、見た目の色で壊れたと分かるようにする
        if (auto* breakable = target.FindComponent<BreakableComponent>())
            breakable->SetActive(false);
        if (auto* collider = target.FindComponent<NS::Object::ColliderComponent>())
            collider->SetActive(false);
        if (auto* mesh = target.FindComponent<NS::Object::MeshRendererComponent>())
            mesh->SetBaseColor(k_BrokenBaseColor);
        // 固形から外すのは壊れた 1 回だけ。直後に動く移動が素通りする
        if (NS::Object::Scene* scene = Owner()->OwningScene())
            scene->SyncPhysics();
    }

    int ImpactResolverComponent::SecondsToSteps(float seconds) const noexcept
    {
        // 見せる単位は秒、数えるのは歩。整数の歩で数えるから同じ入力は同じ長さ止まる
        const float raw = seconds / NS::Core::FrameTimer::FixedDelta();
        if (!std::isfinite(raw))
            return 0;
        return NS::Core::Clamp(static_cast<int>(std::lround(raw)), 0, k_MaxHitStopSteps);
    }

    void ImpactResolverComponent::ReleaseHitStop()
    {
        m_movement->SetActive(true);
        m_movement->SetVelocity(m_pendingSelfVelocity);
        if (m_scaleHeld)
        {
            // 解放の伸びが衝突の後半。伸びる軸は進行の軸と同じで、高さは戻して横だけ伸ばす
            m_stretchScale = ScaledAlongImpact(m_stretchAlong, 1.0f);
            RootTransform().SetScale(m_stretchScale);
            m_recoverRemaining = k_StretchRecoverSteps;
            m_scaleHeld = false;
        }

        // 貫通は段を落とさず猶予も始めない。壊しながら走り続けるループを守る
        const bool wasBreak = m_pendingBreak;
        m_pendingBreak = false;
        if (!wasBreak)
        {
            // 猶予はここから数え始める。止まっている間に数えると、操作できないまま猶予が減る
            m_momentum->BeginGrace();
        }

        NS::Object::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
            return;
        // 相手は id で引き直す。止まっている数歩の間に消されていたら残りだけ諦める
        NS::Object::GameObject* target = scene->World().FindByObjectId(m_pendingTargetId);
        if (target == nullptr)
            return;
        // 食い込みと振動は絵だけ。結果の起点がずれないよう元位置へ厳密に戻してから先へ進む
        target->Root().SetPosition(m_pendingTargetHome);

        // 跡は破壊と押し飛ばしの両方で出す。片方だけ何も残らないと結果が非対称になる
        // 真下の床は相手の中心から探す。箱の中から始まる探索はその箱に当たらず、自分を素通りして床の上面が返る
        bool floorFound = false;
        NS::Core::Vector3 markPosition{0.0f, 0.0f, 0.0f};
        {
            float dist = 0.0f;
            if (NS::Object::ShadowComponent::GroundBelow(
                    m_pendingTargetHome, scene->Physics().Aabbs(), k_MarkProbeDistance, dist))
            {
                floorFound = true;
                markPosition = NS::Core::Vector3{
                    m_pendingTargetHome.x, m_pendingTargetHome.y - dist + k_MarkFloorOffset, m_pendingTargetHome.z};
            }
        }

        if (wasBreak)
        {
            // 破片の飛び方は壊れた物の質量を受け継ぐ
            float mass = 1.0f;
            if (auto* breakable = target->FindComponent<BreakableComponent>())
                mass = breakable->Mass();
            BreakTarget(*target);
            SpawnDebris(m_pendingTargetHome, mass);
            if (floorFound)
                (void)ImpactMarkComponent::SpawnAt(scene, markPosition);
            return;
        }

        // 積み忘れた配置物でも押し飛ばせるよう、無ければその場で足す
        auto* body = target->FindComponent<LaunchedBodyComponent>();
        if (body == nullptr)
            body = target->AddComponent<LaunchedBodyComponent>();
        body->Launch(m_pendingLaunchVelocity);
        if (floorFound)
            (void)ImpactMarkComponent::SpawnAt(scene, markPosition);
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

    void ImpactResolverComponent::SpawnDebris(const NS::Core::Vector3& origin, float mass)
    {
        NS::Object::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr || m_debrisCount <= 0)
            return;
        if (!std::isfinite(mass) || mass < 0.01f)
            mass = 0.01f;
        // 重い物ほど破片が飛ばない。押し飛ばしと同じ向きの質量感を破片でも見せる
        const float speed = m_debrisSpeed / mass;
        for (int i = 0; i < m_debrisCount; ++i)
        {
            // 番号から角度を作る。乱数を使わないので同じ状況では毎回同じ散り方になる
            const float angle = 2.0f * NS::Core::k_Pi * static_cast<float>(i) / static_cast<float>(m_debrisCount);
            // 浮きは交互に変える。全部同じ高さだと 1 つの輪に見えて壊れた量が伝わらない
            const float up = 0.5f + 0.5f * static_cast<float>(i % 2);
            auto owned = std::make_unique<NS::Object::GameObject>();
            owned->Root().SetPosition(origin);
            owned->Root().SetScale(NS::Core::Vector3{k_DebrisScale, k_DebrisScale, k_DebrisScale});
            auto* mesh = owned->AddComponent<NS::Object::MeshRendererComponent>();
            mesh->SetMeshRef("cube");
            mesh->SetMaterialRef("player");
            mesh->SetBaseColor(k_DebrisBaseColor);
            owned->AddComponent<LaunchedBodyComponent>();
            NS::Object::GameObject* spawned = scene->SpawnTransient(std::move(owned));
            if (spawned == nullptr)
                continue;
            // 所有を渡した後の元のポインタは使わない。戻り値から引き直す
            auto* body = spawned->FindComponent<LaunchedBodyComponent>();
            if (body == nullptr)
                continue;
            // 破片だけ寿命を持つ。壊すたびに増えるので、止まったら消さないと世界に積み上がり続ける
            body->SetRestLifeSeconds(m_debrisLifeSeconds);
            body->Launch(NS::Core::Vector3{std::cos(angle) * speed, up * speed, std::sin(angle) * speed});
        }
    }

    int ImpactResolverComponent::ComputeHitStopSteps(float power, float mass, float hitStopScale) const noexcept
    {
        // 質量差をそのまま歩数に出すと停止が伸びすぎるので平方根で圧縮する
        // ピークの倍率は式の最後に掛ける。1.0f は IEEE で恒等なので、ピーク以外の歩数は倍率を掛けない式と一致する
        const float raw =
            m_hitStopBaseSeconds * power * std::sqrt(mass) / NS::Core::FrameTimer::FixedDelta() * hitStopScale;
        if (!std::isfinite(raw))
            return 0;
        const int steps = static_cast<int>(std::lround(raw));
        return NS::Core::Clamp(steps, 0, k_MaxHitStopSteps);
    }

    NS_CLASS(ImpactResolverComponent)
} // namespace NS::Game::Level
