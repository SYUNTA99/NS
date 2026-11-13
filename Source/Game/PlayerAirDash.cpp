#include "Game/PlayerActor.h"

#include "Game/Level/GameScene.h"
#include "Game/Level/HitScreenDirector.h"
#include "Game/Level/ReboundDirections.h"
#include "Game/Level/ReboundPreview.h"
#include "Game/Player/PlayerAcceleration.h"
#include "Game/Player/PlayerJudges.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/PlayerSounds.h"
#include "Game/Player/States/AirDashPlayerState.h"
#include "Game/Player/States/BrakePlayerState.h"
#include "Game/Player/States/DashReboundPlayerState.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/WalkPlayerState.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/IGet/IGetCamera.h"
#include "NSlib/Gfx/Render/DebugDraw.h"
#include "NSlib/Phys/JoltCharacter.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <vector>

namespace
{
    // エアダッシュの状態に結ぶ .actor の動作の名前。事象を置く所で、ダッシュの長さは PlayerParams が持つ
    std::string_view AirDashActionName() noexcept
    {
        return "AirDash";
    }
} // namespace

bool PlayerActor::CanStartAirDash() const noexcept
{
    return m_airDash.m_pressed && AcceptsAirDash();
}

bool PlayerActor::AcceptsAirDash() const noexcept
{
    if (m_airDash.m_remaining <= 0)
    {
        return false;
    }
    if (IsDashRebounding())
    {
        // 操作なしの段は押しを受けない。補正の段に入るとダッシュが 1 回戻る
        return m_airDash.m_steerStarted;
    }
    return m_states->IsCurrent<GL::Player::IdlePlayerState>() || m_states->IsCurrent<GL::Player::WalkPlayerState>() ||
           m_states->IsCurrent<GL::Player::FallPlayerState>() || m_states->IsCurrent<GL::Player::BrakePlayerState>();
}

bool PlayerActor::BeginAirDash() noexcept
{
    // 速さが 0 のダッシュは、終わりの歩も来ないので宙で止まったまま抜けられない
    const float speed = AirDashSpeed();
    if (!NS::IsPositive(speed))
    {
        NS_LOG_WARN(
            Game, ".actor {} のエアダッシュのフレーム数か距離が 0 以下なので、エアダッシュを始めない", ActorFileName());
        return false;
    }
    const NS::Vector3 direction = AirDashStartDirection();
    m_airDash.m_direction = direction;
    m_airDash.m_pressFrames = -1;
    m_airDash.m_reboundPressed = false;
    m_airDash.m_remaining = m_airDash.m_remaining - 1;
    m_facingDir = direction;
    // 押したフレームに玉になって飛び出す。解くのは接地して落ち着いた時 (UncurlWhenSettled)
    // 丸まりは下端を揃えて根を下げる。宙では玉を立ち姿の中心に戻し、押したフレームに半長ぶん下へ飛ばさない
    const bool curlInAir = !IsCurled() && !m_body->IsGrounded();
    SetCurled(true);
    if (curlInAir)
    {
        TransformObj().ShiftPosition(NS::Vector3{0.0f, m_collider->StandingHalfHeight(), 0.0f});
    }
    StartAirControlReturn();
    // 状態に入るのが止めで遅れても、面に当たった時の返る速さがこの控えを読む
    m_airDash.m_dashSpeed = speed;
    m_body->SetVelocity(direction * speed);
    (void)m_states->Change<GL::Player::AirDashPlayerState>();
    return true;
}

NS::Vector3 PlayerActor::AirDashStartDirection() const noexcept
{
    // 反動のためにスティックを回している最中に押した時は、回し始める前の向きへ出す。
    // 止めてから押した時は、押したフレームに入力している向きへ出す
    NS::Vector3 direction{};
    if (m_airDash.m_stickMoving && NS::TryNormalizeHorizontal(m_airDash.m_steadyDirection, direction))
    {
        return direction;
    }
    if (!TryStickDirection(direction) && !NS::TryNormalizeHorizontal(m_facingDir, direction))
    {
        direction = NS::CalcCameraForwardHorizontal(this);
    }
    return direction;
}

#if !defined(NS_SHIPPING)
void PlayerActor::UpdateReboundPreview()
{
    m_reboundPreview->Hide();
    const std::vector<NS::Vector3> table = ReboundTable();
    // 曲げられる先は、止めが明けた時に ChooseReboundDirection が選べる向きと同じ検査で選ぶ
    const auto showArrows = [this, &table](const NS::Vector3& origin, const GL::Level::ReboundHit& rebound) {
        GL::Level::ReboundPreviewArrows arrows{.origin = origin, .assigned = rebound.assigned};
        if (m_params->m_reboundUsesReflection)
        {
            arrows.assignedOnTable = GL::Level::IsTableDirection(table, rebound.assigned);
            // 反射から決める間は、今のスティックで曲げた向きの 1 本だけが曲げられる先
            const NS::Vector3 bent = BendReboundByStick(rebound.assigned, rebound.normal);
            if (!NS::IsNearlyParallel(bent, rebound.assigned))
            {
                arrows.choices.push_back(bent);
            }
            m_reboundPreview->Show(arrows);
            return;
        }
        const GL::Level::ScreenAxes screen = ReboundScreenAxes();
        const GL::Level::ReboundChoiceLimits limits = ReboundLimits();
        for (const NS::Vector3& candidate : table)
        {
            if (GL::Level::CanShiftReboundTo(candidate, rebound, screen, limits))
            {
                arrows.choices.push_back(candidate);
            }
        }
        m_reboundPreview->Show(arrows);
    };

    // 当たった後の止めの間は、スティックで向きを選ぶ時なので、当たった面の矢印を明けで選ぶまで出し続ける
    if (IsDashRebounding() && !m_airDash.m_reboundChosen)
    {
        const GL::Player::TerrainReboundRecord& last = m_airDash.m_last;
        showArrows(last.point - last.normal * m_collider->CapsuleRadius(),
                   GL::Level::ReboundHit{.assigned = last.assigned, .normal = last.normal, .incoming = last.incoming});
        return;
    }

    NS::Vector3 direction{};
    float reach = m_params->m_airDashDistance;
    if (IsAirDashing())
    {
        // 壁に沿って滑る間は出た向きと速度の向きが違うので、実際に進んでいる速度の向きの先を調べる
        direction = m_body->Velocity();
        const float speed = direction.Length();
        if (!NS::HasPositiveMagnitude(speed))
        {
            return;
        }
        direction /= speed;
        // ダッシュが終わった後に触れた壁は反動にならないので、残りの歩で進む距離までだけ調べる
        const std::optional<int> left = AirDashStepsLeft();
        if (left.has_value())
        {
            reach = m_airDash.m_dashSpeed * NS::OS::FrameTimer::FixedDelta() * static_cast<float>(*left);
        }
    }
    else if (AcceptsAirDash())
    {
        direction = AirDashStartDirection();
    }
    else
    {
        return;
    }

    // 押したフレームに玉になる形 (BeginAirDash) で調べる。宙では立ち姿の中心、床の上では下端を揃えた所が玉の中心
    NS::Phys::Capsule ball = m_collider->WorldCapsule();
    if (!IsCurled())
    {
        if (m_body->IsGrounded())
        {
            ball.center.y -= m_collider->CapsuleHalfHeight();
        }
        ball.halfHeight = 0.0f;
    }
    // 反動になる面だけを数える。当て方が欄「反動に数える当て方の下限」より浅い面は、当たっても反動にならない
    NS::Phys::CapsuleCastHit hit{};
    if (!NS::CastCapsuleCollision(m_collider.get(), ball, direction, reach, m_params->m_reboundMinFacing, hit))
    {
        return;
    }
    NS::Vector3 assigned{};
    if (!ReboundBaseDirection(direction, hit.normal, assigned))
    {
        return;
    }
    showArrows(hit.point, GL::Level::ReboundHit{.assigned = assigned, .normal = hit.normal, .incoming = direction});
}
#endif

float PlayerActor::AirDashSpeed() const noexcept
{
    // フレーム数が 0 以下なら割らない。距離は欄の範囲が有限の正の数だけを書く
    if (m_params->m_airDashFrames <= 0)
    {
        return 0.0f;
    }
    return m_params->m_airDashDistance /
           (static_cast<float>(m_params->m_airDashFrames) * NS::OS::FrameTimer::FixedDelta());
}

std::optional<int> PlayerActor::AirDashStepsLeft() const noexcept
{
    // 移る予約の間の歩の数は uint32 の最大で、まだ今のダッシュの歩ではない
    const NS::IStateMachine* machine = GetStateMachine();
    if (machine == nullptr)
    {
        return std::nullopt;
    }
    const NS::uint32 steps = machine->StepCount();
    if (steps == std::numeric_limits<NS::uint32>::max())
    {
        return std::nullopt;
    }
    return std::max(m_params->m_airDashFrames - static_cast<int>(steps), 0);
}

bool PlayerActor::IsAirDashOver() const noexcept
{
    const std::optional<int> left = AirDashStepsLeft();
    return left.has_value() && *left == 0;
}

void PlayerActor::StartAirDashAction() noexcept
{
    // 速さは状態に入った時に控え、途中で欄を書き換えても変えない
    m_airDash.m_dashSpeed = AirDashSpeed();
    // 動作は事象を置く所で、ダッシュの長さを持たない。無くてもダッシュは続け、StartStateAction の WARN だけを残す
    (void)StartStateAction(AirDashActionName());
}

void PlayerActor::KeepAirDashSpeed() noexcept
{
    m_body->SetVelocity(m_airDash.m_direction * m_airDash.m_dashSpeed);
}

void PlayerActor::KeepAirDashExitSpeed() noexcept
{
    m_body->SetVelocity(m_airDash.m_direction * m_params->m_airDashExitSpeed);
}

void PlayerActor::UpdateDashRebound(float dt) noexcept
{
    // 止めの間は状態機械が回らないので、反動の最初の歩が止めの明けたフレームになる
    if (!m_airDash.m_reboundChosen)
    {
        ChooseReboundAtRelease();
    }
    if (m_airDash.m_reboundMissed)
    {
        // 失敗の反動は ChooseReboundAtRelease が落下の状態へ移した。反動の段は進めない
        return;
    }
    ++m_airDash.m_reboundFrames;
    // 操作なしの段で戻る前に呼ぶ。選んだ向きへ回すのは、明けのフレームからの操作なしの段の中
    TurnReboundTowardChosen();
    const int lock = m_params->m_reboundLockFrames;
    // 操作なしの段は、床の上で返っても段のフレーム数まで続ける。返った勢いを見せる長さを当てた所で変えない
    if (m_airDash.m_reboundFrames <= lock)
    {
        return;
    }
    if (GL::Player::PlayerJudgeLand::Judge(m_body->IsGrounded(), m_body->VerticalVelocity()))
    {
        ChangeToWalkOrFall();
        return;
    }
    if (!m_airDash.m_steerStarted)
    {
        m_airDash.m_steerStarted = true;
        m_airDash.m_remaining = 1;
    }
    const int steerEnd = m_params->m_reboundSteerEndFrames;
    if (m_airDash.m_reboundFrames >= steerEnd)
    {
        (void)m_states->Change<GL::Player::FallPlayerState>();
        return;
    }
    if (LedgeGrab())
    {
        return;
    }

    m_body->Gravity(-m_params->m_dashReboundGravity, dt);
    GL::Player::Decelerate(m_body.get(), m_params->m_dashReboundDeceleration, dt);

    // 曲げはスティックの向きだけを目標にする。地形も相手も読まないので、目的地へは向かない
    const NS::Vector3 velocity = m_body->Velocity();
    const float speed = velocity.Length();
    if (NS::IsNearlyZero(speed))
    {
        return;
    }
    const NS::Vector3 current = velocity / speed;
    NS::Vector3 target{};
    if (m_params->m_reboundUsesReflection)
    {
        // 反射から決める間は、明けの曲げと同じ読み方で入力を読む。スティックはカメラの水平の前後左右を指す
        target = BendReboundByStick(current, m_airDash.m_last.normal);
        if (NS::IsNearlyParallel(current, target))
        {
            return;
        }
    }
    else
    {
        // 目標は当たった直後のずらしと同じく画面で倒した向きで、前に倒すと画面の上へ曲がる
        target = ReboundShiftInput(current);
        if (NS::IsNearlyZero(target))
        {
            return;
        }
        target.Normalize();
    }
    const float limitRatio = std::min(m_params->m_reboundSteerLimitRatio, std::nextafter(0.5f, 0.0f));
    const float limit = GL::Level::DirectionStepRadians(m_params->m_reboundTableDivisions) * std::max(limitRatio, 0.0f);
    const int span = steerEnd - lock;
    float progress = 1.0f;
    if (span > 0)
    {
        progress =
            std::clamp(static_cast<float>(m_airDash.m_reboundFrames - lock) / static_cast<float>(span), 0.0f, 1.0f);
    }
    const float rate = NS::ToRadians(NS::Degrees{m_params->m_reboundSteerMaxDegreesPerSecond}).value;
    const float step = std::min(rate * progress * dt, limit - m_airDash.m_steeredRadians);
    if (!NS::IsPositive(step))
    {
        return;
    }
    const NS::Vector3 turned = GL::Level::TurnToward(current, target, step);
    m_airDash.m_steeredRadians += NS::AngleBetweenUnit(current, turned);
    m_body->SetVelocity(turned * speed);
}

void PlayerActor::TurnReboundTowardChosen() noexcept
{
    const GL::Player::TerrainReboundRecord& hit = m_airDash.m_last;
    // 向きを変えなかった反動は、当たったフレームに掛けた速度のまま触らない
    if (NS::IsNearlyParallel(hit.assigned, hit.shifted))
    {
        return;
    }
    // フレーム数を掛けて回すのは反射から決める時だけ。表から選んだ向きへは、明けのフレームに向く
    int bendFrames = 0;
    if (m_params->m_reboundUsesReflection)
    {
        bendFrames = m_params->m_reboundBendFrames;
    }
    const int end = std::max(std::min(bendFrames, m_params->m_reboundLockFrames), 1);
    if (m_airDash.m_reboundFrames > end)
    {
        return;
    }
    const float speed = m_body->Velocity().Length();
    if (!NS::HasPositiveMagnitude(speed))
    {
        return;
    }
    const float whole = NS::AngleBetweenUnit(hit.assigned, hit.shifted);
    const float turned = whole * static_cast<float>(m_airDash.m_reboundFrames) / static_cast<float>(end);
    m_body->SetVelocity(GL::Level::TurnToward(hit.assigned, hit.shifted, turned) * speed);
}

bool PlayerActor::ReboundBaseDirection(const NS::Vector3& incoming,
                                       const NS::Vector3& normal,
                                       NS::Vector3& outDirection) const noexcept
{
    if (m_params->m_reboundUsesReflection)
    {
        NS::Vector3 reflected{};
        if (!GL::Level::ReflectedReboundDirection(incoming, normal, m_params->ReboundMinLeaveRadians(), reflected))
        {
            return false;
        }
        outDirection = SnapReboundToTable(reflected, normal);
        return true;
    }
    return GL::Level::AssignReboundDirection(ReboundTable(), incoming, normal, outDirection);
}

NS::Vector3 PlayerActor::SnapReboundToTable(const NS::Vector3& direction, const NS::Vector3& normal) const noexcept
{
    const std::vector<NS::Vector3> table = ReboundTable();
    return GL::Level::SnapTowardTable(
        table,
        direction,
        normal,
        GL::Level::ReboundSnapLimits{
            .fullRadians = NS::ToRadians(NS::Degrees{m_params->m_reboundSnapFullDegrees}).value,
            .blendRadians = NS::ToRadians(NS::Degrees{m_params->m_reboundSnapBlendDegrees}).value,
            .minLeaveRadians = m_params->ReboundMinLeaveRadians()});
}

NS::Vector3 PlayerActor::SnapInputToTable(const NS::Vector3& input) const noexcept
{
    const std::vector<NS::Vector3> table = ReboundTable();
    // 入力は面に沿う向きも面へ向かう向きも取るので、面から離れる角度では表の向きを外さない
    return GL::Level::SnapTowardTable(
        table,
        input,
        input,
        GL::Level::ReboundSnapLimits{
            .fullRadians = NS::ToRadians(NS::Degrees{m_params->m_reboundSnapFullDegrees}).value,
            .blendRadians = NS::ToRadians(NS::Degrees{m_params->m_reboundSnapBlendDegrees}).value,
            .minLeaveRadians = -NS::k_Pi / 2.0f});
}

NS::Vector3 PlayerActor::BendReboundByStick(const NS::Vector3& base, const NS::Vector3& normal) const noexcept
{
    if (m_input->DesiredSpeedScale() < m_params->StickDeadzone())
    {
        return base;
    }
    // そろえるのは下の SnapInputToTable の 1 回だけ。先に画面でそろえると、カメラの角度で隣の表の向きが選ばれる
    NS::Vector3 input = GL::Level::TurnIntoSurfaceUpward(m_input->DesiredDirection(), normal);
    if (NS::IsNearlyZero(input))
    {
        return base;
    }
    // そろえるのは世界の向きにした入力の方。曲げた結果をそろえると、一番近い表の向きが base の時に曲げが消える
    input.Normalize();
    input = SnapInputToTable(input);
    return GL::Level::BendReboundTowardInput(
        base,
        input,
        normal,
        GL::Level::ReboundBendLimits{.strength = m_params->m_reboundInputStrength,
                                     .minLeaveRadians = m_params->ReboundMinLeaveRadians()});
}

float PlayerActor::MoveMaxSlopeRadians() const noexcept
{
    // 登れない坂に触れていると、離れる向きへ飛ぶ時でも水平に坂へ向かう分が消え、選んだ向きと速さで飛ばない
    if (IsDashRebounding() && m_airDash.m_reboundChosen &&
        m_airDash.m_reboundFrames <= m_params->m_reboundLockFrames &&
        NS::HasPositiveMagnitude(NS::Dot(m_body->Velocity(), m_airDash.m_last.normal)))
    {
        return m_params->ReboundLeaveMaxSlopeRadians();
    }
    return m_params->MaxSlopeRadians();
}

void PlayerActor::TryBeginTerrainRebound() noexcept
{
    const bool dashing = IsAirDashing();
    if ((!dashing && !IsDashRebounding()) || !m_body->IsBlocked())
    {
        return;
    }
    NS::Vector3 incoming = m_airDash.m_intended;
    const float speed = incoming.Length();
    if (!NS::HasPositiveMagnitude(speed))
    {
        return;
    }
    incoming /= speed;
    const NS::Vector3 normal = m_body->BlockNormal();
    if (-NS::Dot(incoming, normal) < m_params->m_reboundMinFacing)
    {
        return;
    }
    // 反動の続きで床や坂に落ちたら着地にする。エアダッシュは坂でも返る
    if (!dashing && NS::Phys::IsWalkableNormal(normal.y, m_params->MaxSlopeRadians()))
    {
        return;
    }
    float baseSpeed = speed;
    if (dashing)
    {
        baseSpeed = m_airDash.m_dashSpeed * m_params->m_terrainReboundSpeedScale;
    }
    else
    {
        // 返った後に続けて当たる時は、前のダッシュの押しを持ち越さない。この止めの間の押しを新しく受ける
        m_airDash.m_pressFrames = -1;
    }
    BeginTerrainRebound(incoming, normal, baseSpeed);
}

void PlayerActor::BeginTerrainRebound(const NS::Vector3& incoming, const NS::Vector3& normal, float baseSpeed) noexcept
{
    NS::Vector3 assigned{};
    if (!ReboundBaseDirection(incoming, normal, assigned))
    {
        return;
    }

    // 返る速さは当て方で減らさない。入力が返る向きと合った時だけ ApplyReboundChoice が強める
    const float facing = std::min(-NS::Dot(incoming, normal), 1.0f);
    // 反射から決める時は、ダッシュの間の最初の押しが当たるフレームに近ければ時機が合った押し。
    // 止めの間の押しも合った押しなので、合わなかったかは止めの明けに ChooseReboundAtRelease が決める
    m_airDash.m_reboundPressed = m_params->m_reboundUsesReflection && m_airDash.m_pressFrames >= 0 &&
                                 m_airDash.m_pressFrames <= m_params->m_reboundPressLeadFrames;
    m_airDash.m_reboundMissed = false;
    const float speed = baseSpeed;
    m_body->SetVelocity(assigned * speed);

    NS::Vector3 reflected = GL::Level::ReflectAcross(incoming, normal);
    reflected.Normalize();
    m_airDash.m_last = GL::Player::TerrainReboundRecord{.point = TransformObj().Position(),
                                                        .incoming = incoming,
                                                        .normal = normal,
                                                        .reflected = reflected,
                                                        .assigned = assigned,
                                                        .shifted = assigned,
                                                        .facing = facing,
                                                        .speed = speed};
    m_airDash.m_reboundCount = m_airDash.m_reboundCount + 1;
    m_airDash.m_reboundFrames = 0;
    m_airDash.m_steeredRadians = 0.0f;
    m_airDash.m_steerStarted = false;
    m_airDash.m_reboundChosen = false;
    m_airDash.m_reboundStopFrames = 1;
    StartAirControlReturn();
    (void)m_states->Change<GL::Player::DashReboundPlayerState>();
    m_playerSounds->PlayRebound(this);
    const NS::Vector3 contact = TransformObj().Position() - normal * m_collider->CapsuleRadius();
    if (!m_params->m_reboundUsesReflection)
    {
        // 入力が合ったかは明けまで分からないので、当たったフレームの束は当て方だけで選ぶ
        std::string_view name = "地形の反動[弱い]";
        if (facing >= m_params->m_reboundFullFacing)
        {
            name = "地形の反動[全部]";
        }
        StartTerrainReboundReaction(contact, name);
    }
    else
    {
        // 反射から決める時は、押す時機が合ったかと、返る向きが表の向きかで束を分ける。止めの間の押しで替わる
        StartTerrainReboundReaction(contact, ReflectedReboundReactionName());
    }
    // 止めの間の押しも受ける。向きは明けのフレームに ChooseReboundAtRelease が選ぶ
    RequestHitStop(m_params->m_reboundStopMinFrames);
}

void PlayerActor::ObserveReboundPress() noexcept
{
    if (m_airDash.m_pressFrames >= 0)
    {
        // 数える押しは最初の 1 回だけ。押し直しを読むと、連打で必ず時機が合う
        if (IsAirDashing())
        {
            m_airDash.m_pressFrames = m_airDash.m_pressFrames + 1;
        }
        return;
    }
    if (!m_airDash.m_pressed)
    {
        return;
    }
    // ダッシュを出した押しのフレームはまだダッシュの状態でないので、ここを通らない
    if (IsAirDashing())
    {
        m_airDash.m_pressFrames = 0;
        return;
    }
    if (!m_params->m_reboundUsesReflection || !IsDashRebounding() || m_airDash.m_reboundChosen)
    {
        return;
    }
    // 止めの間の押しは、時機が合った押し。当たったフレームに始めた失敗の束を、押したこのフレームに成功の束へ替える
    m_airDash.m_pressFrames = 0;
    m_airDash.m_reboundPressed = true;
    const GL::Player::TerrainReboundRecord& hit = m_airDash.m_last;
    StartTerrainReboundReaction(hit.point - hit.normal * m_collider->CapsuleRadius(), ReflectedReboundReactionName());
}

void PlayerActor::ObserveStick() noexcept
{
    // カメラで回す前の入力で比べる。世界の向きで比べると、カメラを回すだけで動いたとみなす
    const NS::Vector3 stick{m_input->StickRight(), 0.0f, m_input->StickForward()};
    m_airDash.m_stickMoving = (stick - m_airDash.m_prevStick).Length() > m_params->m_reboundStopStickChange;
    m_airDash.m_prevStick = stick;
    if (m_airDash.m_stickMoving)
    {
        return;
    }
    // 真ん中から入力し始めた時は止まっていた向きが無いので、押せば入力した向きへ出る
    NS::Vector3 direction{};
    if (!TryStickDirection(direction))
    {
        direction = NS::Vector3{};
    }
    m_airDash.m_steadyDirection = direction;
}

void PlayerActor::ExtendReboundStop() noexcept
{
    // 延ばすのは、地形に当たってから向きを選ぶまでの間だけ。ほかの当たりの止めと、向きを選んだ後の止めは延ばさない
    if (!IsDashRebounding() || m_airDash.m_reboundChosen)
    {
        return;
    }
    if (IsHitStopping())
    {
        m_airDash.m_reboundStopFrames = m_airDash.m_reboundStopFrames + 1;
    }
    // 1 フレーム足すのは、止めの最後のフレームか、止めが明けて向きを選ぶはずのフレームだけ。
    // 明けるフレームで足すとそのフレームから止まるので、最短が 1
    // 以下でも延ばせる。止めの残りを数えるのは基盤の止めだけにする
    if (!m_airDash.m_stickMoving || HitStopFramesRemaining() > 1 ||
        m_airDash.m_reboundStopFrames >= m_params->m_reboundStopMaxFrames)
    {
        return;
    }
    if (!IsHitStopping())
    {
        m_airDash.m_reboundStopFrames = m_airDash.m_reboundStopFrames + 1;
    }
    RequestHitStop(HitStopFramesRemaining() + 1);
}

void PlayerActor::ChooseReboundAtRelease() noexcept
{
    m_airDash.m_reboundChosen = true;
    if (m_params->m_reboundUsesReflection && !m_airDash.m_reboundPressed)
    {
        // 反射から決める時は、時機の合った押しが無い反動は失敗。押さなかった時も、早すぎた時も同じ。
        // 勢いを失って、当て方の向きへ小さく跳ね返る。玉を解いて落下の状態へ戻すので、反動は続かず、
        // 続けて面に当たっても返らない。エアダッシュは着地まで戻らない
        m_airDash.m_reboundMissed = true;
        GL::Player::TerrainReboundRecord& missed = m_airDash.m_last;
        missed.speed = missed.speed * m_params->m_reboundMissedSpeedScale;
        m_body->SetVelocity(missed.assigned * missed.speed);
        SetCurled(false);
        (void)m_states->Change<GL::Player::FallPlayerState>();
        return;
    }
    const GL::Player::TerrainReboundRecord& hit = m_airDash.m_last;
    const GL::Level::ReboundChoice choice = ChooseReboundInput();
    // 当たったフレームが弱い束だった時に入力が合ったら、選んだこのフレームに強い束へ替える。
    // 反射から決める時は、強い束を出すかをボタンを押す時機で決めたので替えない
    if (!m_params->m_reboundUsesReflection && choice.result == GL::Level::ReboundInputResult::Matched &&
        hit.facing < m_params->m_reboundFullFacing)
    {
        StartTerrainReboundReaction(hit.point - hit.normal * m_collider->CapsuleRadius(), "地形の反動[全部]");
    }
    ApplyReboundChoice(choice);
}

std::string_view PlayerActor::ReflectedReboundReactionName() const
{
    if (!m_airDash.m_reboundPressed)
    {
        return "地形の反動[失敗]";
    }
    // 表の向きへ返る時は、当たる前の矢印が黄になる。同じ条件で強い束にする
    if (GL::Level::IsTableDirection(ReboundTable(), m_airDash.m_last.assigned))
    {
        return "地形の反動[全部]";
    }
    return "地形の反動[弱い]";
}

void PlayerActor::StartTerrainReboundReaction(const NS::Vector3& contact, std::string_view name) noexcept
{
    NS::HitReaction* reaction = HitReactionClassObj();
    NS::HitSensor* self = m_bodyHitSensor;
    if (reaction == nullptr || self == nullptr)
    {
        return;
    }
    // 前の反動の残像と揺れは、新しい反動の束で始め直す
    StopTackleHitEffects();
    for (const NS::HitOwnerRun& run : StartHitReactionRuns(name, contact, self, self))
    {
        if (run.item.name == "TackleGhost")
        {
            m_ghost->Start(this, run);
        }
    }
}

void PlayerActor::DrawTerrainRebound() const
{
    if (!m_params->m_drawReboundLines || m_airDash.m_reboundCount == 0)
    {
        return;
    }
    const GL::Player::TerrainReboundRecord& hit = m_airDash.m_last;
    const float length = m_params->m_reboundLineLength;
    const NS::Vector3& from = hit.point;
    // 白は当たる前の向き、青は面の法線、水色は物理の反射、緑は表の向き、紫はずらした後の返った向き
    NS::Gfx::DrawDebugLine(from - hit.incoming * length, from, NS::Color{1.0f, 1.0f, 1.0f, 1.0f});
    NS::Gfx::DrawDebugLine(from, from + hit.normal * length, NS::Color{0.0f, 0.0f, 1.0f, 1.0f});
    NS::Gfx::DrawDebugLine(from, from + hit.reflected * length, NS::Color{0.0f, 1.0f, 1.0f, 1.0f});
    NS::Gfx::DrawDebugLine(from, from + hit.assigned * length, NS::Color{0.0f, 1.0f, 0.0f, 1.0f});
    NS::Gfx::DrawDebugLine(from, from + hit.shifted * length, NS::Color{1.0f, 0.0f, 1.0f, 1.0f});
    if (!IsDashRebounding())
    {
        return;
    }
    // 今の進む向き (曲げの後) は、表の向きなら黄、それ以外は灰。輪は段の色で、赤が操作なし、黄が補正
    NS::Vector3 now = m_body->Velocity();
    now.Normalize();
    const NS::Vector3 center = TransformObj().Position();
    NS::Color nowColor{0.6f, 0.6f, 0.6f, 1.0f};
    if (GL::Level::IsTableDirection(ReboundTable(), now))
    {
        nowColor = NS::Color{1.0f, 1.0f, 0.0f, 1.0f};
    }
    NS::Gfx::DrawDebugLine(center, center + now * length, nowColor);
    NS::Color stage{1.0f, 0.0f, 0.0f, 1.0f};
    if (m_airDash.m_steerStarted)
    {
        stage = NS::Color{1.0f, 1.0f, 0.0f, 1.0f};
    }
    const float radius = m_collider->CapsuleRadius();
    NS::Gfx::DrawDebugCircle(center, NS::Vector3{radius, 0.0f, 0.0f}, NS::Vector3{0.0f, 0.0f, radius}, stage);
}

std::vector<NS::Vector3> PlayerActor::ReboundTable() const
{
    GL::Level::ReboundTableShape shape = GL::Level::ReboundTableShape::ThreePlanes;
    if (m_params->m_reboundTableWithPitch)
    {
        shape = GL::Level::ReboundTableShape::YawPitch;
    }
    return GL::Level::MakeReboundDirections(m_params->m_reboundTableDivisions, shape);
}

GL::Level::ReboundChoice PlayerActor::ChooseReboundInput() const noexcept
{
    const GL::Player::TerrainReboundRecord& hit = m_airDash.m_last;
    if (m_input->DesiredSpeedScale() < m_params->StickDeadzone())
    {
        return GL::Level::ReboundChoice{.direction = hit.assigned};
    }
    if (m_params->m_reboundUsesReflection)
    {
        // 曲げた角度が、画面で同じに見える角度の差以下なら、入力が返る向きと合ったと数える
        const NS::Vector3 bent = BendReboundByStick(hit.assigned, hit.normal);
        GL::Level::ReboundInputResult result = GL::Level::ReboundInputResult::Matched;
        if (NS::AngleBetweenUnit(hit.assigned, bent) > ReboundLimits().screenTieRadians)
        {
            result = GL::Level::ReboundInputResult::Shifted;
        }
        return GL::Level::ReboundChoice{.direction = bent, .result = result};
    }
    return GL::Level::ChooseReboundDirection(
        ReboundTable(),
        GL::Level::ReboundHit{.assigned = hit.assigned, .normal = hit.normal, .incoming = hit.incoming},
        ReboundScreenAxes(),
        m_input->StickRight(),
        m_input->StickForward(),
        ReboundLimits());
}

GL::Level::ReboundChoiceLimits PlayerActor::ReboundLimits() const noexcept
{
    return GL::Level::ReboundChoiceLimits{
        .screenTieRadians = NS::ToRadians(NS::Degrees{m_params->m_reboundScreenTieDegrees}).value,
        .depthSlackRadians = NS::ToRadians(NS::Degrees{m_params->m_reboundDepthSlackDegrees}).value,
        .stepRadians = GL::Level::DirectionStepRadians(m_params->m_reboundTableDivisions),
        .matchVisibleLength = m_params->m_reboundMatchVisibleLength};
}

void PlayerActor::ApplyReboundChoice(const GL::Level::ReboundChoice& choice) noexcept
{
    if (choice.result == GL::Level::ReboundInputResult::Kept)
    {
        return;
    }
    GL::Player::TerrainReboundRecord& hit = m_airDash.m_last;
    hit.input = choice.result;
    if (choice.result == GL::Level::ReboundInputResult::Matched)
    {
        // 続けて当たる時は今の速さで返るので、合うたびに倍率が積もらないよう、エアダッシュで当てた速さの倍率までにする。
        // 落ちて当てた速さが上限を超えていても、合った時に遅くはしない
        const float scale = m_params->m_reboundMatchedSpeedScale;
        float matched = hit.speed * scale;
        const float dashMatched = m_airDash.m_dashSpeed * m_params->m_terrainReboundSpeedScale * scale;
        if (NS::HasPositiveMagnitude(dashMatched))
        {
            matched = std::max(hit.speed, std::min(matched, dashMatched));
        }
        if (NS::HasPositiveMagnitude(hit.speed))
        {
            m_body->SetVelocity(m_body->Velocity() * (matched / hit.speed));
            hit.speed = matched;
        }
        // 割り当てた向きと画面で同じに見える、奥と手前の動きが少ない向きを選んだ時は、その向きへ飛ぶ。
        // 速度を向けるのは、この後の TurnReboundTowardChosen
        hit.shifted = choice.direction;
        return;
    }
    hit.shifted = choice.direction;
    // 反射から決める時は、倒して押して向きを変えるのが成功なので、当たったフレームの束を弱めない。
    // 表から選ぶ時だけ、隣の向きへずれた事を弱めた演出と重ねた振動で知らせる
    if (!m_params->m_reboundUsesReflection)
    {
        StartShiftedReboundReaction();
    }
}

void PlayerActor::StartShiftedReboundReaction() noexcept
{
    NS::HitReaction* reaction = HitReactionClassObj();
    NS::HitSensor* self = m_bodyHitSensor;
    if (reaction == nullptr || self == nullptr)
    {
        return;
    }
    const float scale = m_params->m_reboundShiftedEffectScale;
    reaction->ScaleRunning(NS::HitReactionSelf{.actor = this, .model = ModelClassObj()}, scale);
    if (GL::Level::HitScreenDirector* screen = GL::Level::HitScreenOf(this))
    {
        screen->Scale(this, scale);
    }
    m_ghost->ScaleOpacity(scale);
    (void)StartHitReaction("地形の反動[ずらし]", TransformObj().Position(), self, self);
}

GL::Level::ScreenAxes PlayerActor::ReboundScreenAxes() const noexcept
{
    // 水平の右と前は、スティックを世界へ回す入力用の行列の行をそのまま使う。画面の上と見る向きはカメラの傾きから作る
    const NS::Matrix& input = m_input->InputMatrix();
    const NS::Vector3 right{input._11, input._12, input._13};
    const NS::Vector3 flatForward{input._31, input._32, input._33};
    NS::Vector3 forward = flatForward;
    const std::optional<NS::CameraPose> pose = NS::GetCameraViewPose(this);
    if (pose.has_value())
    {
        const NS::Vector3 look = pose->target - pose->position;
        if (NS::HasPositiveMagnitude(look))
        {
            forward = look;
            forward.Normalize();
        }
    }
    NS::Vector3 up = NS::Cross(forward, right);
    up.Normalize();
    return GL::Level::ScreenAxes{.right = right, .up = up, .forward = forward, .flatForward = flatForward};
}

NS::Vector3 PlayerActor::ReboundShiftInput(const NS::Vector3& assigned) const noexcept
{
    if (m_input->DesiredSpeedScale() < m_params->StickDeadzone())
    {
        return NS::Vector3{};
    }
    const GL::Level::ScreenAxes screen = ReboundScreenAxes();
    return GL::Level::ShiftInputOnScreen(assigned,
                                         screen.right,
                                         screen.up,
                                         screen.forward,
                                         m_input->StickRight(),
                                         m_input->StickForward());
}
