#include "Game/PlayerActor.h"

#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/SlamBall.h"
#include "Game/Player/States/BeforeContactPlayerState.h"
#include "Game/Player/States/BodySlamPlayerState.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/LedgeClimbingPlayerState.h"
#include "Game/Player/States/LedgeHangingPlayerState.h"
#include "Game/Player/States/ReboundPlayerState.h"
#include "Game/Player/States/WalkPlayerState.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Action/ActionTimeline.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/CoreObject/Scene/Scene.h"

#include <algorithm>
#include <cmath>

namespace
{
    // 回転数を秒で割った回る速さ (度/秒)。秒が 0 以下か有限でない時は 0
    [[nodiscard]] float SpinSpeedFor(float turns, float seconds) noexcept
    {
        if (!std::isfinite(seconds) || !NS::IsPositive(seconds))
        {
            return 0.0f;
        }
        return turns * 360.0f / seconds;
    }
} // namespace

bool PlayerActor::AcceptsBodySlamRequest() const noexcept { return !IsBodySlamming() && CanMoveBody(); }

void PlayerActor::RequestBodySlam(float charge01, float overcharge01) noexcept
{
    if (!AcceptsBodySlamRequest())
    {
        return;
    }
    // そのフレームで出せないと押しが無言で消える。ジャンプと同じ先行入力時間だけ覚える
    m_request.bufferRemaining = m_params->m_jumpBufferTime;
    // 非数は 0..1 への丸めを素通りして溜め量に残るため、入口で 0 へ倒す
    if (!std::isfinite(charge01))
    {
        m_request.charge01 = 0.0f;
    }
    else
    {
        m_request.charge01 = NS::Clamp(charge01, 0.0f, 1.0f);
    }
    m_request.overcharge01 = 0.0f;
    if (std::isfinite(overcharge01))
    {
        m_request.overcharge01 = NS::Clamp(overcharge01, 0.0f, 1.0f);
    }
    // 残すと、先行入力のうちに来た通常突進が前の溜めた突進の向きへ出る
    m_request.hasDir = false;
    m_request.verticalSpeed = 0.0f;
}

void PlayerActor::RequestBodySlam(float charge01,
                                  const NS::Vector3& aimDirection,
                                  float launchVerticalSpeed,
                                  float overcharge01) noexcept
{
    if (!AcceptsBodySlamRequest())
    {
        return;
    }
    RequestBodySlam(charge01, overcharge01);
    NS::Vector3 dir{};
    if (!NS::TryNormalizeHorizontal(aimDirection, dir))
    {
        return;
    }
    // 非数と無限の向きは正規化を通り抜ける
    if (!std::isfinite(dir.x) || !std::isfinite(dir.z))
    {
        return;
    }
    m_request.dir = dir;
    m_request.hasDir = true;
    if (std::isfinite(launchVerticalSpeed))
    {
        m_request.verticalSpeed = launchVerticalSpeed;
    }
}

float PlayerActor::BodySlamSpeed() const noexcept
{
    if (m_slam.m_isTap)
    {
        return m_params->m_tapSlamSpeed;
    }
    return m_params->m_bodySlamSpeed;
}

NS::Vector3 PlayerActor::BodySlamVelocity() const noexcept
{
    const NS::Body& body = *m_body;
    if (!IsBodySlamming())
    {
        return body.Velocity();
    }

    const float speed = BodySlamSpeed();
    return NS::Vector3{m_slam.m_dir.x * speed, body.VerticalVelocity(), m_slam.m_dir.z * speed};
}

GL::Level::BodySlamMotion PlayerActor::SlamMotion() const noexcept
{
    const bool slamming = IsBodySlamming();
    float remaining = 0.0f;
    if (slamming)
    {
        remaining = std::max(m_slam.m_distanceTarget - m_slam.m_travelled, 0.0f);
    }
    // 当たる直前の動作がどの段にも無ければ、入る先が無いので線を引かない
    const NS::ActionTimeline* actions = ActionTimelineClassObj();
    const bool predicts =
        actions != nullptr &&
        (actions->FindAction(GL::Level::BeforeContactActionName(GL::Level::HitTier::Center)) != nullptr ||
         actions->FindAction(GL::Level::BeforeContactActionName(GL::Level::HitTier::Wide)) != nullptr);
    return GL::Level::BodySlamMotion{.slamming = slamming,
                                     .velocity = BodySlamVelocity(),
                                     .charge01 = m_slam.Charge01(),
                                     .overcharge01 = m_slam.Overcharge01(),
                                     .remainingDistance = remaining,
                                     .predictsContact = predicts};
}

GL::Level::ImpactSelf PlayerActor::SelfForImpact() const noexcept
{
    return GL::Level::ImpactSelf{.sensors = GetHitSensorDirector(), .position = Root().Position(), .ignore = this};
}

void PlayerActor::DecideBeforeContact(const GL::Level::ImpactAnswer& answer) noexcept
{
    // 検知したフレームは、突進を打ち切った所で当たる直前を出ている
    if (answer.cancelBodySlam)
    {
        return;
    }
    const std::optional<GL::Level::SlamLineTarget>& predicted = answer.predicted;
    if (IsInBeforeContact())
    {
        if (predicted.has_value() && predicted->target == m_beforeContact.target &&
            predicted->tier == m_beforeContact.tier)
        {
            return;
        }
        // 予測が外れたか、相手か段が変わった。形と自機以外の止めは出る時に戻る
        (void)m_states->Change<GL::Player::BodySlamPlayerState>();
        return;
    }
    if (!IsBodySlamming() || !predicted.has_value())
    {
        return;
    }
    const NS::ActorAction* action =
        ActionTimelineClassObj()->FindAction(GL::Level::BeforeContactActionName(predicted->tier));
    if (action == nullptr)
    {
        return;
    }
    // 動作の最後のフレームが検知のフレーム。予測が遅れた時は、残りの歩に合うフレームから入る
    const int contactFrame = NS::ActionLength(*action) - 1;
    const int frames = answer.framesToPredictedContact;
    if (frames > contactFrame)
    {
        return;
    }
    m_beforeContact = BeforeContactEntry{.target = predicted->target,
                                         .tier = predicted->tier,
                                         .startFrame = contactFrame - frames,
                                         .point = predicted->origin + predicted->direction * predicted->contact};
    // 新しい当たりは、前の当たりの束で走っている演出を打ち切る
    StopTackleHitEffects();
    if (NS::HitReaction* reaction = HitReactionClassObj())
    {
        reaction->Stop(this);
    }
    (void)m_states->Change<GL::Player::BeforeContactPlayerState>();
}

bool PlayerActor::ContinuesBeforeContact(const GL::Level::ImpactRecord& impact) const noexcept
{
    return IsInBeforeContact() && m_beforeContact.target->Id() == impact.targetId && m_beforeContact.tier == impact.tier;
}

void PlayerActor::BeginBeforeContact() noexcept
{
    NS::SceneActor* target = m_beforeContact.target;
    NS_ASSERT(Game, target != nullptr, "当たる直前の状態へ入る予約が相手を書いていない");
    if (target->BodySensorClassObj() == nullptr)
    {
        NS_LOG_WARN(Game, "予測した相手に体のセンサーが無いので、当たる直前の動作を始めない: 相手 {}", target->Id());
        return;
    }
    (void)ActionTimelineClassObj()->Start(ActionTimelineSelfOf(),
                                          GL::Level::BeforeContactActionName(m_beforeContact.tier),
                                          m_beforeContact.startFrame,
                                          m_beforeContact.point,
                                          BodySensorClassObj(),
                                          target->BodySensorClassObj());
}

void PlayerActor::EndBeforeContact() noexcept
{
    // 検知で当たりへ移る時は、形と自機以外の止めを触れる瞬間まで残す
    if (m_resolver->IsHitAwaitingEntry())
    {
        return;
    }
    ActionTimelineClassObj()->Stop();
    StopTackleHitEffects();
    if (NS::HitReaction* reaction = HitReactionClassObj())
    {
        reaction->Stop(this);
    }
    OwningScene()->HoldOthers(this, 0);
}

void PlayerActor::CancelBodySlam() noexcept
{
    if (!IsBodySlamming())
    {
        return;
    }
    EndBodySlam();
}

void PlayerActor::EndBodySlam() noexcept
{
    NS::Body& body = *m_body;
    m_slam.m_travelled = 0.0f;
    m_slam.m_distanceTarget = 0.0f;

    // 加速は最高速を超えた速さを削らない。切らないと、倒している間は突進の速さのまま走り続ける
    const NS::Vector3 lateral = body.LateralVelocity();
    const float speed = NS::HorizontalLength(lateral);
    const float cap = MaxSpeed();
    if (speed > cap)
    {
        body.SetLateralVelocity(NS::HorizontalPart(lateral) * (cap / speed));
    }

    ChangeToWalkOrFall();
    m_playerEvents.onBodySlamEnded.Invoke();
}

void PlayerActor::ChangeToWalkOrFall() noexcept
{
    if (m_body->IsGrounded())
    {
        (void)m_states->Change<GL::Player::WalkPlayerState>();
    }
    else
    {
        (void)m_states->Change<GL::Player::FallPlayerState>();
    }
}

void PlayerActor::AdvanceBodySlamTravel(const NS::Vector3& delta) noexcept
{
    if (!IsBodySlamming())
    {
        return;
    }

    // 進んだ距離は実際に動いた量から測る。突進の速さから積むと壁で止められたフレームも進んだ扱いになる
    const float stepDistance = NS::HorizontalLength(delta);
    m_slam.m_travelled += stepDistance;

    // 進めないフレームで打ち切る。壁で止められると進んだ距離が伸びず、突進から出られなくなる
    // 発動したフレームは見ない。ここで打ち切ると発動から打ち切りまでに ImpactResolver が
    // 一度も走らず、突進を見ないまま終わる
    const bool stalled = !m_slam.m_justStarted && NS::IsNearlyZero(stepDistance);
    m_slam.m_justStarted = false;

    if (m_slam.m_travelled >= m_slam.m_distanceTarget || stalled)
    {
        EndBodySlam();
    }
}

NS::Vector3 PlayerActor::AimDirection() const noexcept
{
    NS::Vector3 dir{};
    // 反発後の滑りなど残った速度が向きに勝つと狙いと食い違う方へ飛ぶ。入力が無ければ速度よりカメラの前を先に見る
    if (NS::TryNormalizeHorizontal(DesiredDirection(), dir))
    {
        return dir;
    }
    if (NS::TryNormalizeHorizontal(NS::CalcCameraForwardHorizontal(this), dir))
    {
        return dir;
    }
    if (NS::TryNormalizeHorizontal(m_body->LateralVelocity(), dir))
    {
        return dir;
    }
    return NS::Vector3{0.0f, 0.0f, 0.0f};
}

void PlayerActor::MarkBodySlamAim() noexcept
{
    m_request.aimDir = AimDirection();
    m_request.aimAge = 0.0f;
}

float PlayerActor::BodySlamAimBlend01() const noexcept
{
    const float hold = m_params->m_slamAimHoldTime;
    const float fade = m_params->m_slamAimFadeTime;
    const float age = m_request.aimAge;
    if (age <= hold)
    {
        return 1.0f;
    }
    // 巻き戻し秒を消える秒より後ろにできる。幅が 0 以下なら割らずに切る
    if (!NS::IsGreaterThan(fade, hold) || age >= fade)
    {
        return 0.0f;
    }
    return (fade - age) / (fade - hold);
}

NS::Vector3 PlayerActor::BlendedAimDirection() const noexcept
{
    const NS::Vector3 dir = AimDirection();
    const float aimLength = NS::HorizontalLength(m_request.aimDir);
    if (NS::IsNearlyZero(aimLength))
    {
        return dir;
    }
    const float blend = BodySlamAimBlend01();
    if (!NS::IsPositive(blend))
    {
        return dir;
    }
    const NS::Vector3 mixed = dir * (1.0f - blend) + m_request.aimDir * blend;
    // 正反対の向きを同じくらいの重みで混ぜると長さが 0 近くになる。その時は濃い側をそのまま採る
    NS::Vector3 mixedDirection{};
    if (NS::TryNormalizeHorizontal(mixed, mixedDirection))
    {
        return mixedDirection;
    }
    if (blend >= 0.5f)
    {
        return m_request.aimDir;
    }
    return dir;
}

NS::Vector3 PlayerActor::ReleaseDirection() const noexcept
{
    // しきい値を越えて放すと、AdvanceCharge が控えた狙いの線の向きを添えて頼む。越える前に放すと通常突進になる
    GL::Level::AimLine line{};
    NS::Vector3 lineDirection{};
    if (m_charge.m_judge.IsCharging() && m_charge.TryGetAimLine(line) &&
        NS::TryNormalizeHorizontal(line.direction, lineDirection))
    {
        return lineDirection;
    }
    return BlendedAimDirection();
}

bool PlayerActor::BodySlam() noexcept
{
    NS::Body& body = *m_body;
    // 速度を書く前に読む。発動のフレームはまだ前のフレームの接地のまま
    const bool launchedInAir = !body.IsGrounded();
    // 添えた向きは放す前に見せていた狙いなので、入力も押したフレームの控えも混ぜない
    NS::Vector3 dir = m_request.dir;
    if (!m_request.hasDir)
    {
        dir = BlendedAimDirection();
    }

    if (NS::IsNearlyZeroHorizontal(dir))
    {
        return false;
    }

    m_slam.m_charge01 = m_request.charge01;
    m_slam.m_overcharge01 = m_request.overcharge01;
    m_slam.m_isTap = !NS::IsPositive(m_request.charge01);
    m_slam.m_travelled = 0.0f;
    m_slam.m_justStarted = true;
    const float speed = BodySlamSpeed();

    if (m_slam.m_isTap)
    {
        m_slam.m_distanceTarget = m_params->m_tapSlamDistance;
        body.SetVelocity(NS::Vector3{dir.x * speed, m_params->m_tapSlamUpSpeed, dir.z * speed});
    }
    else
    {
        m_slam.m_distanceTarget = m_params->m_bodySlamDistance;
        // ジャンプの縦速度を捨て、狙いと同じ道筋で放つ。ジャンプで角度上限を越えないようにする
        float vertical = 0.0f;
        if (m_request.hasDir)
        {
            vertical = m_request.verticalSpeed;
        }
        body.SetVelocity(NS::Vector3{dir.x * speed, vertical, dir.z * speed});
    }
    // 到達までの回転数を保つ角速度。到達時間が無効なら 0
    float turns = m_params->m_chargedSlamTurns;
    if (m_slam.m_isTap)
    {
        turns = m_params->m_tapSlamTurns;
    }
    m_slam.m_spinSpeed = SpinSpeedFor(turns, m_slam.m_distanceTarget / speed);

    // 距離が 0 以下だと 1 フレーム目で終わって発動が消えるため、出さずに通常移動のままにする
    if (!NS::IsPositive(m_slam.m_distanceTarget))
    {
        return false;
    }

    // 出せた時だけ書く。反動の後のカメラと放した瞬間の絵が、突進の後も最後に出た突進の向きとして読む
    m_slam.m_dir = dir;
    m_request.hasDir = false;
    // 溜めすぎで控えた突進なら、判定が頼み直しを止めて放すまで溜め無しになる
    m_slam.m_forced = m_charge.m_judge.IsAwaitingLaunch();
    m_charge.m_judge.MarkLaunched();
    // 地面から出した突進は数えない。数えると、浮いて当てた通常突進や上向きに放った突進の反動で空中の 1 発が出ない
    if (launchedInAir)
    {
        m_request.spent = true;
    }
    // 突進はどの経路で出ても玉で走らせる。掴まり中に放した押しは予約に残り、先行入力の秒の内に
    // 縁を離れれば出るが、その時の丸まりは掴まりで解けている
    ChangeCurled(true);

    (void)m_states->Change<GL::Player::BodySlamPlayerState>();
    m_playerEvents.onBodySlamStarted.Invoke();
    return true;
}

bool PlayerActor::BeginRebound(const GL::Player::ReboundArc& arc) noexcept
{
    // 曲線にならない反動で移すと、弾かれないまま速度が 0 に消える。移さずに偽を返し、速度は呼び手に任せる
    const NS::Vector3 velocity = ReboundVelocityFor(arc);
    if (!NS::IsPositive(velocity.y))
    {
        return false;
    }
    NS::Vector3 direction{};
    if (!NS::TryNormalizeHorizontal(arc.direction, direction))
    {
        return false;
    }

    // 反動は最後に出した突進が当たって始まるので、その突進が通常突進だったかで回転数を選ぶ
    float turns = m_params->m_chargedReboundTurns;
    if (m_slam.m_isTap)
    {
        turns = m_params->m_tapReboundTurns;
    }
    m_rebound.m_direction = direction;
    m_rebound.m_missTumble = arc.missTumble;
    ++m_rebound.m_count;
    m_rebound.m_spinSpeed = SpinSpeedFor(turns, GL::Level::LaunchArcFlightSeconds(m_params->ReboundLaunchArc(arc)));
    m_body->SetVelocity(velocity);
    StartAirControlReturn();
    (void)m_states->Change<GL::Player::ReboundPlayerState>();
    return true;
}

NS::Vector3 PlayerActor::ReboundVelocityFor(const GL::Player::ReboundArc& arc) const noexcept
{
    return GL::Level::LaunchArcInitialVelocity(m_params->ReboundLaunchArc(arc));
}

void PlayerActor::SetCurled(bool curled) noexcept
{
    // 掴まりからは突進が出ない。玉のままぶら下がると、押しても突進が出ないのに玉の見た目だけが残る
    // 掴まっている間に玉にすると縁を測り直す手の高さが下がり、押したフレームに縁を放して 0.5 m 落ちた
    if (curled && (m_states->IsCurrent<GL::Player::LedgeHangingPlayerState>() ||
                   m_states->IsCurrent<GL::Player::LedgeClimbingPlayerState>()))
    {
        return;
    }
    ChangeCurled(curled);
}

bool PlayerActor::IsCurled() const noexcept { return m_collider->IsSphereShape(); }

void PlayerActor::ChangeCurled(bool curled) noexcept
{
    NS::Collider& collider = *m_collider;
    if (curled == IsCurled())
    {
        return;
    }
    collider.SetSphereShape(curled);
    if (curled)
    {
        m_appearance->Curl(m_model.get());
    }
    else
    {
        m_appearance->Uncurl(m_model.get());
    }
    // 半長ぶん根を上げ下げして、立ち姿と玉の当たりの下端を揃える
    float rise = collider.StandingHalfHeight();
    if (curled)
    {
        rise = -rise;
    }
    // TODO: 低い天井で立ち姿へ戻す検査は無い。現在のコースでは次の接触解決に任せる
    // 形の変更を動きとして補間しないよう、前フレームの位置もずらす
    Root().ShiftPosition(NS::Vector3{0.0f, rise, 0.0f});
}

NS::Sphere PlayerActor::SlamBallAt(const NS::Vector3& rootPosition) const noexcept
{
    return GL::Player::SlamBallAt(m_collider.get(), rootPosition);
}

void PlayerActor::UncurlWhenSettled() noexcept
{
    NS::Body& body = *m_body;
    if (!IsCurled())
    {
        return;
    }
    // 押している間は着地しても丸まりを解かない
    if (m_charge.m_judge.IsHeld() || IsBodySlamming() || !body.IsGrounded())
    {
        return;
    }
    // 床の上でダッシュしても、床の上で返っても、その間は玉のまま
    if (IsAirDashing() || IsDashRebounding())
    {
        return;
    }
    // 外れの着地からこすって止まる間は玉のまま地面をこする
    if (IsSkidding())
    {
        return;
    }
    // 反動が明けたフレームは接地のフラグが残ったまま上向きの速度が入る。速度を見ないと宙へ出る前に解ける
    if (body.VerticalVelocity() > 0.0f)
    {
        return;
    }
    // 当てたフレームは、決定の段で裁定の答えを読んで先に突進を終える。今の状態だけを見ると、当てた瞬間に解ける
    if (m_slam.m_wasSlamming)
    {
        return;
    }
    // 放したフレームに出せなかった突進は予約に残る。解くと、予約から出るまでの間だけ立ち姿に戻る
    if (m_request.bufferRemaining > 0.0f)
    {
        return;
    }
    ChangeCurled(false);
}
