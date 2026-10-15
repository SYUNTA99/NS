#include "Game/PlayerActor.h"

#include "Game/Level/ImpactResolver.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerParams.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Graphics/DebugDraw.h"
#include "NSlib/Object/IUse/IUseCamera.h"
#include "NSlib/Object/SubObjects/Body.h"
#include "NSlib/Object/SubObjects/Collider.h"

#include <algorithm>
#include <cmath>


namespace
{
    // ImpactInputJudge は固定ステップの整数で数えるため、秒の欄をフレーム数へ丸めて渡す
    [[nodiscard]] int SecondsToSteps(float seconds, float dt) noexcept
    {
        if (!(dt > 0.0f))
        {
            return 0;
        }
        const float raw = seconds / dt;
        if (!std::isfinite(raw))
        {
            return 0;
        }
        return std::max(0, static_cast<int>(std::lround(raw)));
    }

    // 紫の揺れの位相 (ラジアン)。紫の間の速さ f = f0 + (f1 − f0) (t / T)^2 を 0 から t まで積んだ閉じた式で、
    // 足し込む値を別に持たないので台本の再生と食い違わない。始まりの位相は、紫になりきった t = T で π/2 を通るように
    // 決める。紫になりきった後は終わりの速さで進む
    [[nodiscard]] float OverchargeSwayPhase(float seconds,
                                            float overchargeSeconds,
                                            float startRate,
                                            float endRate) noexcept
    {
        if (!(overchargeSeconds > 0.0f))
        {
            return (0.5f * NS::k_Pi) + (2.0f * NS::k_Pi) * endRate * seconds;
        }
        const float rise = endRate - startRate;
        const float phaseAtEdge = (2.0f * NS::k_Pi) * (startRate * overchargeSeconds + rise * overchargeSeconds / 3.0f);
        if (seconds >= overchargeSeconds)
        {
            return (0.5f * NS::k_Pi) + (2.0f * NS::k_Pi) * endRate * (seconds - overchargeSeconds);
        }
        const float travelled =
            (2.0f * NS::k_Pi) *
            (startRate * seconds + rise * seconds * seconds * seconds / (3.0f * overchargeSeconds * overchargeSeconds));
        return (0.5f * NS::k_Pi) - phaseAtEdge + travelled;
    }
} // namespace

const GL::Level::ImpactInputJudge& PlayerActor::ChargeJudge() const noexcept
{
    return m_charge.Judge();
}

bool PlayerActor::TryGetAimLine(GL::Level::AimLine& outLine) const noexcept
{
    return m_charge.TryGetAimLine(outLine);
}

NS::Vector3 PlayerActor::ChargeAimDirection() const noexcept
{
    GL::Level::AimLine line{};
    if (m_charge.TryGetAimLine(line))
    {
        return line.direction;
    }
    return AimDirection();
}

bool PlayerActor::TryGetAimTarget(GL::Level::SlamLineTarget& outTarget) const noexcept
{
    return m_charge.TryGetAimTarget(outTarget);
}

bool PlayerActor::TryGetLineTarget(GL::Level::SlamLineTarget& outTarget) const noexcept
{
    return m_charge.TryGetLineTarget(outTarget);
}

float PlayerActor::StanceHeight() const noexcept
{
    return m_appearance->StanceHeight(m_charge);
}

void PlayerActor::ObserveCharge(bool held)
{
    m_chargeObserved.held = held;
    m_chargeObserved.hasAimTarget = false;
    m_chargeObserved.hasAimLine = false;
    if (!held)
    {
        return;
    }
    // 向きは効果を掛ける前の遊びの視点から読む。描画の割合と実カメラに依らない
    if (GetCameraManager() == nullptr)
    {
        return;
    }
    const NS::Vector3 direction = NS::Obj::CameraForwardHorizontal(this);
    if (!std::isfinite(direction.x) || !std::isfinite(direction.z))
    {
        return;
    }
    m_chargeObserved.aimLine = GL::Level::AimLine{.origin = Root().Position(),
                                                  .direction = direction,
                                                  .length = BodySlamDistance(),
                                                  .launchVerticalSpeed = 0.0f,
                                                  .grounded = m_body->IsGrounded()};
    m_chargeObserved.hasAimLine = true;
    m_chargeObserved.hasAimTarget = m_resolver->FindSlamLineTarget(m_chargeObserved.aimLine.direction,
                                                                   m_chargeObserved.aimLine.length,
                                                                   m_params.get(),
                                                                   m_body.get(),
                                                                   m_collider.get(),
                                                                   m_chargeObserved.aimTarget);
    // 放す時に添える縦の速さ。届かない相手と相手が無い時は 0 で水平に放つ
    if (m_chargeObserved.hasAimTarget)
    {
        m_chargeObserved.aimLine.launchVerticalSpeed = m_chargeObserved.aimTarget.launchVerticalSpeed;
    }
}

void PlayerActor::AdvanceCharge(float dt)
{
    GL::Level::ImpactInputJudge& judge = m_charge.m_judge;
    judge.chargeThresholdSteps = SecondsToSteps(m_params->m_chargeThresholdSeconds, dt);
    judge.chargeMaxSteps = SecondsToSteps(m_params->m_chargeFullSeconds, dt);
    judge.overchargeSteps = SecondsToSteps(m_params->m_overchargeSeconds, dt);
    judge.Step(m_chargeObserved.held);

    if (judge.JustPressed())
    {
        MarkBodySlamAim();
    }
    // 溜めに入るのを待たずに、押したフレームから丸まる。押したフレームだけ入れると、押したまま出直した後に
    // 丸まりが戻らない。溜めすぎで出た後も押している間は丸まったまま
    if (judge.IsHeld())
    {
        SetCurled(true);
    }
    m_bodySlamHeld = judge.IsHeld();

    // 控えた線は放す前のフレームに矢印を貼った線で、放したフレームはまだ引き直していない。溜めて放した突進は
    // スティックを見ずにその向きへ出す。通常突進は矢印が出ないので入力の向きへ出す
    // 紫で放した時と勝手に出た時は、控えた線に放す前のフレームの揺れが入っている
    const auto requestCharged = [this](float charge01, float overcharge01) {
        if (m_charge.m_hasAimLine)
        {
            RequestBodySlam(charge01, m_charge.m_aimLine.direction, m_charge.m_aimLine.launchVerticalSpeed, overcharge01);
        }
        else
        {
            RequestBodySlam(charge01, overcharge01);
        }
    };
    const GL::Level::SlamKind fired = judge.TakeFired();
    if (fired == GL::Level::SlamKind::Charged)
    {
        requestCharged(judge.Charge01(), judge.Overcharge01());
        NS_LOG_INFO(Game, "体当たり発動: {} 溜め {:.2f}", GL::Level::SlamKindLabel(fired), judge.Charge01());
    }
    else if (fired == GL::Level::SlamKind::Tap)
    {
        RequestBodySlam(0.0f);
        NS_LOG_INFO(Game, "体当たり発動: {} 溜め {:.2f}", GL::Level::SlamKindLabel(fired), 0.0f);
    }
    else if (judge.IsAwaitingLaunch())
    {
        // 溜めすぎで控えた突進は、出せない間 (止め・突進の最中) の頼みが捨てられるので、出るまで毎フレーム頼み直す
        // 向きは出たフレームの狙いの線。出たかは BodySlam が判定へ知らせ、次のフレームからここへ来ない
        requestCharged(judge.Charge01(), judge.Overcharge01());
    }

    float scale = 1.0f;
    if (judge.IsCharging())
    {
        scale = m_params->ChargingSpeedScale();
    }
    SetMaxSpeedScale(scale);

    if (judge.JustStartedCharging())
    {
        // 縦は残す。空中で溜めたフレームに 0 を書くと落下が一瞬止まって引っかかる
        const NS::Vector3 velocity = m_body->Velocity();
        m_body->SetVelocity(NS::Vector3{0.0f, velocity.y, 0.0f});
    }

    m_charge.m_hasAimLine = m_chargeObserved.hasAimLine;
    m_charge.m_hasAimTarget = m_chargeObserved.hasAimTarget;
    m_charge.m_aimLine = m_chargeObserved.aimLine;
    m_charge.m_aimTarget = m_chargeObserved.aimTarget;
    if (m_charge.m_hasAimLine)
    {
        m_charge.m_aimLine.origin = Root().Position();
    }
    if (m_charge.m_hasAimTarget)
    {
        m_charge.m_aimTarget.origin = Root().Position();
    }
    ApplyChargeSway(dt);
}

void PlayerActor::ApplyChargeSway(float dt)
{
    const GL::Level::ImpactInputJudge& judge = m_charge.m_judge;
    const int steps = judge.OverchargedSteps();
    // 紫に入ったフレームに数える。離したフレームは控えた値が残るので、押している間だけ見る
    if (steps == 1 && judge.IsHeld())
    {
        ++m_overchargeCount;
    }
    m_charge.m_lineTarget = m_charge.m_aimTarget;
    m_charge.m_hasLineTarget = m_charge.m_hasAimTarget;
    m_charge.m_swayPhase = 0.0f;
    m_charge.m_swayOffset = 0.0f;
    m_charge.m_swayReachedEdge = false;
    if (steps <= 0 || !m_charge.m_hasAimLine)
    {
        m_charge.m_hasSwayEdge = false;
        return;
    }

    // 秒は欄でなく判定のフレーム数から出す。紫になりきったフレームで seconds と overchargeSeconds が同じ値になり、
    // 位相がちょうど端に来る
    const float seconds = static_cast<float>(steps) * dt;
    const float overchargeSeconds = static_cast<float>(judge.overchargeSteps) * dt;
    m_charge.m_swayPhase = OverchargeSwayPhase(
        seconds, overchargeSeconds, m_params->m_overchargeSwayStartRate, m_params->m_overchargeSwayEndRate);
    // 振れ幅は紫の深さの 2 乗。前半は真ん中に収まって強くなるだけの区間、後半で外れまで振れる
    float depth = 1.0f;
    if (overchargeSeconds > 0.0f && seconds < overchargeSeconds)
    {
        depth = seconds / overchargeSeconds;
    }
    // 紫になりきった時に来る端を、紫に入るたびに入れ替える
    float side = 1.0f;
    if (m_overchargeCount % 2 == 0)
    {
        side = -1.0f;
    }
    m_charge.m_swayOffset = side * m_params->m_overchargeSwayMaxOffset * depth * depth * std::sin(m_charge.m_swayPhase);
    // 端は位相が π/2 + nπ を通る所。数を別に持たないので、矢印と火花の拍がずれない
    const int edge = static_cast<int>(std::floor((m_charge.m_swayPhase - 0.5f * NS::k_Pi) / NS::k_Pi));
    m_charge.m_swayReachedEdge = m_charge.m_hasSwayEdge && edge > m_charge.m_swayEdge;
    m_charge.m_swayEdge = edge;
    m_charge.m_hasSwayEdge = true;

    float distance = m_params->m_overchargeSwayFallbackDistance;
    if (m_charge.m_hasAimTarget)
    {
        distance = m_charge.m_aimTarget.along;
    }
    const NS::Vector3 forward = m_charge.m_aimLine.direction;
    const NS::Vector3 right{forward.z, 0.0f, -forward.x};
    NS::Vector3 swayed{};
    // 距離が 0 か非数で向きが作れない時は揺らさない
    if (!(distance > 0.0f) || !NS::TryNormalizeHorizontal(forward * distance + right * m_charge.m_swayOffset, swayed) ||
        !std::isfinite(swayed.x) || !std::isfinite(swayed.z))
    {
        m_charge.m_swayOffset = 0.0f;
        return;
    }
    // 上下の角度は揺れていない線の相手へ合わせた縦の速さのまま。揺れは左右だけ
    m_charge.m_aimLine.direction = swayed;
    m_charge.m_hasLineTarget = m_resolver->FindSlamLineTarget(swayed,
                                                              m_charge.m_aimLine.length,
                                                              m_params.get(),
                                                              m_body.get(),
                                                              m_collider.get(),
                                                              m_charge.m_lineTarget);
    if (m_charge.m_hasLineTarget)
    {
        m_charge.m_lineTarget.origin = Root().Position();
    }
}

void PlayerActor::CancelCharge() noexcept
{
    // 判定のしきい値の歩数は AdvanceCharge が毎回入れ直すので失われない
    m_charge = GL::Player::ChargeStatus{};
    m_chargeObserved = ChargeObservation{};
    // 押しの印が真の間、自機は着地しても丸まりを解かない。印を書くのは押しを裁いた歩だけなので、ここで偽へ戻す
    m_bodySlamHeld = false;
    SetMaxSpeedScale(1.0f);
}

void PlayerActor::EndCharge() noexcept
{
    CancelCharge();
    // 外れた後は着地で丸まりを解く歩が来ないので、ここで解く。ResetState は速度と状態機械まで戻すので呼ばない
    SetCurled(false);
}

#if !defined(NS_SHIPPING)
void PlayerActor::DrawChargeRing() const
{
    if (!m_charge.m_judge.IsCharging())
    {
        return;
    }

    const float charge01 = m_charge.m_judge.Charge01();
    // 輪は当たりのカプセルの下端 (中心 − 軸 × (半分の高さ + 半径)) に置く
    const NS::Phys::Capsule capsule = m_collider->CapsuleAt(Root().Position());
    const NS::Vector3 foot = capsule.center - capsule.axis * (capsule.halfHeight + capsule.radius);
    // 溜め量が輪の広がりに出ないと、満タンまでの途中が読めない
    const float radius = m_collider->CapsuleRadius() + 0.25f + charge01 * 0.75f;
    NS::Color color{1.0f, 0.85f, 0.2f, 1.0f};
    if (m_charge.m_judge.IsChargeFull())
    {
        color = NS::Color{1.0f, 1.0f, 1.0f, 1.0f};
    }
    NS::Gfx::DebugDraw::Circle(NS::Vector3{foot.x, foot.y + 0.05f, foot.z},
                               NS::Vector3{radius, 0.0f, 0.0f},
                               NS::Vector3{0.0f, 0.0f, radius},
                               color);
}
#endif
