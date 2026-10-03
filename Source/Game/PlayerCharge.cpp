#include "Game/Player.h"

#include "Game/Level/ImpactResolver.h"
#include "Game/Player/PlayerParams.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/DebugDraw.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/Collider.h"
#include "Runtime/Object/IUse/IUseCamera.h"

#include <algorithm>
#include <cmath>

// ---- 溜めと狙い ----

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
        constexpr float k_TwoPi = 2.0f * NS::Core::k_Pi;
        constexpr float k_EdgePhase = 0.5f * NS::Core::k_Pi;
        if (!(overchargeSeconds > 0.0f))
        {
            return k_EdgePhase + k_TwoPi * endRate * seconds;
        }
        const float rise = endRate - startRate;
        const float phaseAtEdge = k_TwoPi * (startRate * overchargeSeconds + rise * overchargeSeconds / 3.0f);
        if (seconds >= overchargeSeconds)
        {
            return k_EdgePhase + k_TwoPi * endRate * (seconds - overchargeSeconds);
        }
        const float travelled = k_TwoPi * (startRate * seconds + rise * seconds * seconds * seconds /
                                                                     (3.0f * overchargeSeconds * overchargeSeconds));
        return k_EdgePhase - phaseAtEdge + travelled;
    }
} // namespace

const NS::Game::Level::ImpactInputJudge& Player::ChargeJudge() const noexcept
{
    return m_charge.judge;
}

bool Player::TryGetAimLine(NS::Game::Level::AimLine& outLine) const noexcept
{
    if (!m_charge.hasAimLine)
    {
        return false;
    }
    outLine = m_charge.aimLine;
    return true;
}

bool Player::TryGetAimTarget(NS::Game::Level::SlamLineTarget& outTarget) const noexcept
{
    if (!m_charge.hasAimTarget)
    {
        return false;
    }
    outTarget = m_charge.aimTarget;
    return true;
}

bool Player::TryGetLineTarget(NS::Game::Level::SlamLineTarget& outTarget) const noexcept
{
    if (!m_charge.hasLineTarget)
    {
        return false;
    }
    outTarget = m_charge.lineTarget;
    return true;
}

float Player::StanceHeight() const noexcept
{
    if (!m_charge.judge.IsHoldingCharge())
    {
        return 1.0f;
    }
    if (m_charge.judge.IsCharging())
    {
        return m_params->m_chargeSquashScale;
    }
    return m_params->m_pressSquashScale;
}

void Player::ObserveCharge(bool held)
{
    m_charge.observedHeld = held;
    m_charge.observedHasAimTarget = false;
    m_charge.observedHasAimLine = false;
    if (!held)
    {
        return;
    }
    // 向きは効果を掛ける前の遊びの視点から読む。描画の割合と実カメラに依らない
    if (GetCameraManager() == nullptr)
    {
        return;
    }
    const NS::Core::Vector3 direction = NS::Obj::CameraForwardHorizontal(*this);
    if (!std::isfinite(direction.x) || !std::isfinite(direction.z))
    {
        return;
    }
    m_charge.observedAimLine = NS::Game::Level::AimLine{.origin = Root().Position(),
                                                        .direction = direction,
                                                        .length = BodySlamDistance(),
                                                        .launchVerticalSpeed = 0.0f,
                                                        .grounded = m_body->IsGrounded()};
    m_charge.observedHasAimLine = true;
    m_charge.observedHasAimTarget = m_resolver->FindSlamLineTarget(
        m_charge.observedAimLine.direction, m_charge.observedAimLine.length, m_charge.observedAimTarget);
    // 放す時に添える縦の速さ。届かない相手と相手が無い時は 0 で水平に放つ
    if (m_charge.observedHasAimTarget)
    {
        m_charge.observedAimLine.launchVerticalSpeed = m_charge.observedAimTarget.launchVerticalSpeed;
    }
}

void Player::AdvanceCharge(float dt)
{
    NS::Game::Level::ImpactInputJudge& judge = m_charge.judge;
    judge.chargeThresholdSteps = SecondsToSteps(m_params->m_chargeThresholdSeconds, dt);
    judge.chargeMaxSteps = SecondsToSteps(m_params->m_chargeFullSeconds, dt);
    judge.overchargeSteps = SecondsToSteps(m_params->m_overchargeSeconds, dt);
    judge.Step(m_charge.observedHeld);

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
    SetBodySlamHeld(judge.IsHeld());

    // 控えた線は放す前のフレームに矢印を貼った線で、放したフレームはまだ引き直していない。溜めて放した突進は
    // スティックを見ずにその向きへ出す。タップは矢印が出ないので入力の向きへ出す
    // 紫で放した時と勝手に出た時は、控えた線に放す前のフレームの揺れが入っている
    const auto requestCharged = [this](float charge01, float overcharge01) {
        if (m_charge.hasAimLine)
        {
            RequestBodySlam(charge01, m_charge.aimLine.direction, m_charge.aimLine.launchVerticalSpeed, overcharge01);
        }
        else
        {
            RequestBodySlam(charge01, overcharge01);
        }
    };
    const NS::Game::Level::SlamKind fired = judge.TakeFired();
    if (fired == NS::Game::Level::SlamKind::Charged)
    {
        requestCharged(judge.Charge01(), judge.Overcharge01());
        NS_LOG_INFO(Game, "体当たり発動: {} 溜め {:.2f}", NS::Game::Level::SlamKindLabel(fired), judge.Charge01());
    }
    else if (fired == NS::Game::Level::SlamKind::Tap)
    {
        RequestBodySlam(0.0f);
        NS_LOG_INFO(Game, "体当たり発動: {} 溜め {:.2f}", NS::Game::Level::SlamKindLabel(fired), 0.0f);
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
        const NS::Core::Vector3 velocity = m_body->Velocity();
        m_body->SetVelocity(NS::Core::Vector3{0.0f, velocity.y, 0.0f});
    }

    m_charge.hasAimLine = m_charge.observedHasAimLine;
    m_charge.hasAimTarget = m_charge.observedHasAimTarget;
    m_charge.aimLine = m_charge.observedAimLine;
    m_charge.aimTarget = m_charge.observedAimTarget;
    if (m_charge.hasAimLine)
    {
        m_charge.aimLine.origin = Root().Position();
    }
    if (m_charge.hasAimTarget)
    {
        m_charge.aimTarget.origin = Root().Position();
    }
    ApplyChargeSway(dt);
}

void Player::ApplyChargeSway(float dt)
{
    const NS::Game::Level::ImpactInputJudge& judge = m_charge.judge;
    const int steps = judge.OverchargedSteps();
    // 紫に入ったフレームに数える。離したフレームは控えた値が残るので、押している間だけ見る
    if (steps == 1 && judge.IsHeld())
    {
        ++m_overchargeCount;
    }
    m_charge.lineTarget = m_charge.aimTarget;
    m_charge.hasLineTarget = m_charge.hasAimTarget;
    m_charge.swayPhase = 0.0f;
    m_charge.swayOffset = 0.0f;
    if (steps <= 0 || !m_charge.hasAimLine)
    {
        return;
    }

    const float seconds = static_cast<float>(steps) * dt;
    const float overchargeSeconds = m_params->m_overchargeSeconds;
    m_charge.swayPhase = OverchargeSwayPhase(
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
    m_charge.swayOffset = side * m_params->m_overchargeSwayMaxOffset * depth * depth * std::sin(m_charge.swayPhase);

    float distance = m_params->m_overchargeSwayFallbackDistance;
    if (m_charge.hasAimTarget)
    {
        distance = m_charge.aimTarget.along;
    }
    const NS::Core::Vector3 forward = m_charge.aimLine.direction;
    const NS::Core::Vector3 right{forward.z, 0.0f, -forward.x};
    NS::Core::Vector3 swayed{};
    // 距離が 0 か非数で向きが作れない時は揺らさない
    if (!(distance > 0.0f) ||
        !NS::Core::TryNormalizeHorizontal(forward * distance + right * m_charge.swayOffset, swayed) ||
        !std::isfinite(swayed.x) || !std::isfinite(swayed.z))
    {
        m_charge.swayOffset = 0.0f;
        return;
    }
    // 上下の角度は揺れていない線の相手へ合わせた縦の速さのまま。揺れは左右だけ
    m_charge.aimLine.direction = swayed;
    m_charge.hasLineTarget = m_resolver->FindSlamLineTarget(swayed, m_charge.aimLine.length, m_charge.lineTarget);
    if (m_charge.hasLineTarget)
    {
        m_charge.lineTarget.origin = Root().Position();
    }
}

void Player::CancelCharge() noexcept
{
    // 判定のしきい値の歩数は AdvanceCharge が毎回入れ直すので失われない
    m_charge = ChargeRecord{};
    // 押しの印が真の間、自機は着地しても丸まりを解かない。印を書くのは押しを裁いた歩だけなので、ここで偽へ戻す
    SetBodySlamHeld(false);
    SetMaxSpeedScale(1.0f);
}

void Player::EndCharge() noexcept
{
    CancelCharge();
    // 外れた後は着地で丸まりを解く歩が来ないので、ここで解く。ResetState は速度と状態機械まで戻すので呼ばない
    SetCurled(false);
}

#if !defined(NS_SHIPPING)
void Player::DrawChargeRing() const
{
    if (!m_charge.judge.IsCharging())
    {
        return;
    }

    const float charge01 = m_charge.judge.Charge01();
    // 輪は当たりのカプセルの下端 (中心 − 軸 × (半分の高さ + 半径)) に置く
    const NS::Phys::Capsule capsule = m_collider->CapsuleAt(Root().Position());
    const NS::Core::Vector3 foot = capsule.center - capsule.axis * (capsule.halfHeight + capsule.radius);
    // 溜め量が輪の広がりに出ないと、満タンまでの途中が読めない
    const float radius = m_collider->CapsuleRadius() + 0.25f + charge01 * 0.75f;
    NS::Core::Color color{1.0f, 0.85f, 0.2f, 1.0f};
    if (m_charge.judge.IsChargeFull())
    {
        color = NS::Core::Color{1.0f, 1.0f, 1.0f, 1.0f};
    }
    NS::Gfx::DebugDraw::Circle(NS::Core::Vector3{foot.x, foot.y + 0.05f, foot.z},
                               NS::Core::Vector3{radius, 0.0f, 0.0f},
                               NS::Core::Vector3{0.0f, 0.0f, radius},
                               color);
}
#endif
