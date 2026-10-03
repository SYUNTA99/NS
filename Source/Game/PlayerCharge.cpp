#include "Game/Player.h"

#include "Game/Level/ImpactResolver.h"
#include "Game/Player/PlayerParams.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/DebugDraw.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/CameraComponent.h"
#include "Runtime/Object/Scene/Scene.h"

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

float Player::StanceHeight() const noexcept
{
    if (!m_charge.judge.IsHeld())
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
    NS::Obj::Scene* scene = OwningScene();
    if (scene == nullptr)
    {
        return;
    }
    const NS::Obj::CameraComponent* camera = scene->MainCamera();
    if (camera == nullptr)
    {
        return;
    }
    NS::Core::Vector3 direction{};
    if (!NS::Core::TryNormalizeHorizontal(camera->ForwardHorizontal(), direction))
    {
        return;
    }
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
    judge.Step(m_charge.observedHeld);

    if (judge.JustPressed())
    {
        MarkBodySlamAim();
    }
    // 溜めに入るのを待たずに、押したフレームから丸まる。押したフレームだけ入れると、押したまま出直した後に
    // 丸まりが戻らない
    if (judge.IsHeld())
    {
        SetCurled(true);
    }
    SetBodySlamHeld(judge.IsHeld());

    const NS::Game::Level::SlamKind fired = judge.TakeFired();
    if (fired != NS::Game::Level::SlamKind::None)
    {
        float charge01 = 0.0f;
        if (fired == NS::Game::Level::SlamKind::Charged)
        {
            charge01 = judge.Charge01();
        }
        // 控えた線は放す前のフレームに矢印を貼った線で、放したフレームはまだ引き直していない。溜めて放した突進は
        // スティックを見ずにその向きへ出す。タップは矢印が出ないので入力の向きへ出す
        if (fired == NS::Game::Level::SlamKind::Charged && m_charge.hasAimLine)
        {
            RequestBodySlam(charge01, m_charge.aimLine.direction, m_charge.aimLine.launchVerticalSpeed);
        }
        else
        {
            RequestBodySlam(charge01);
        }
        NS_LOG_INFO(Game, "体当たり発動: {} 溜め {:.2f}", NS::Game::Level::SlamKindLabel(fired), charge01);
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
    const NS::Phys::Capsule capsule = m_body->CapsuleAt(Root().Position());
    const NS::Core::Vector3 foot = capsule.center - capsule.axis * (capsule.halfHeight + capsule.radius);
    // 溜め量が輪の広がりに出ないと、満タンまでの途中が読めない
    const float radius = m_body->CapsuleRadius() + 0.25f + charge01 * 0.75f;
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
