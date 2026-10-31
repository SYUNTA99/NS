#include "Game/PlayerActor.h"

#include "Game/Level/ReboundDirections.h"
#include "Game/Player/PlayerAcceleration.h"
#include "Game/Player/PlayerJudges.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/PlayerSounds.h"
#include "Game/Player/States/AirDashPlayerState.h"
#include "Game/Player/States/BrakePlayerState.h"
#include "Game/Player/States/DashReboundPlayerState.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/SkidPlayerState.h"
#include "Game/Player/States/WalkPlayerState.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/CoreObject/Action/ActionTimeline.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/IGet/IGetCamera.h"
#include "NSlib/Gfx/Render/DebugDraw.h"
#include "NSlib/Phys/JoltCharacter.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

namespace
{
    // 2 つが同じか真逆で回す面が決まらない時は v のまま
    NS::Vector3 TurnLike(const NS::Vector3& v, const NS::Vector3& from, const NS::Vector3& to) noexcept
    {
        NS::Vector3 side{NS::RejectUnit(to, from)};
        if (NS::IsNearlyZero(side))
        {
            return v;
        }
        side.Normalize();
        const float cosine = std::clamp(NS::Dot(from, to), -1.0f, 1.0f);
        const float sine = std::sqrt(1.0f - cosine * cosine);
        const float along = NS::Dot(v, from);
        const float across = NS::Dot(v, side);
        const NS::Vector3 rest = v - from * along - side * across;
        return from * (along * cosine - across * sine) + side * (along * sine + across * cosine) + rest;
    }

    // エアダッシュの状態に結ぶ .actor の動作の名前。長さ (length) がダッシュの長さを持つ
    std::string_view AirDashActionName() noexcept
    {
        return "AirDash";
    }
} // namespace

bool PlayerActor::CanStartAirDash() const noexcept
{
    if (!m_airDash.m_pressed || m_airDash.m_remaining <= 0)
    {
        return false;
    }
    if (IsDashRebounding())
    {
        // 操作なしの段は押しを受けない。補正の段に入るとダッシュが 1 回戻る
        return m_airDash.m_steerStarted;
    }
    return m_states->IsCurrent<GL::Player::IdlePlayerState>() || m_states->IsCurrent<GL::Player::WalkPlayerState>() ||
           m_states->IsCurrent<GL::Player::FallPlayerState>() || m_states->IsCurrent<GL::Player::BrakePlayerState>() ||
           m_states->IsCurrent<GL::Player::SkidPlayerState>();
}

bool PlayerActor::BeginAirDash() noexcept
{
    // 速さが 0 のダッシュは、状態の終わり (IsStateEnd) も来ないので宙で止まったまま抜けられない
    const float speed = AirDashSpeed();
    if (!NS::IsPositive(speed))
    {
        NS_LOG_WARN(Game,
                    ".actor {} の動作 {} が無いか長さが 0、または距離が 0 以下なので、エアダッシュを始めない",
                    ActorFileName(),
                    AirDashActionName());
        return false;
    }
    // 押したフレームに倒している向きへ出す。倒し直してすぐ押しても、前の向きへ出ない
    NS::Vector3 direction{};
    const bool stick = DesiredSpeedScale() >= m_params->StickDeadzone() &&
                       NS::TryNormalizeHorizontal(DesiredDirection(), direction);
    if (!stick && !NS::TryNormalizeHorizontal(m_facingDir, direction))
    {
        direction = NS::CalcCameraForwardHorizontal(this);
    }
    m_airDash.m_direction = direction;
    m_airDash.m_remaining = m_airDash.m_remaining - 1;
    m_facingDir = direction;
    // 押したフレームに玉になって飛び出す。解くのは接地して落ち着いた時 (UncurlWhenSettled)
    // 丸まりは下端を揃えて根を下げる。宙では玉を立ち姿の中心に戻し、押したフレームに半長ぶん下へ飛ばさない
    const bool curlInAir = !IsCurled() && !m_body->IsGrounded();
    SetCurled(true);
    if (curlInAir)
    {
        Root().ShiftPosition(NS::Vector3{0.0f, m_collider->StandingHalfHeight(), 0.0f});
    }
    StartAirControlReturn();
    // 状態に入るのが止めで遅れても、面に当たった時の返る速さがこの控えを読む
    m_airDash.m_dashSpeed = speed;
    m_body->SetVelocity(direction * speed);
    (void)m_states->Change<GL::Player::AirDashPlayerState>();
    return true;
}

float PlayerActor::AirDashSpeed() const noexcept
{
    const NS::ActionTimeline* timeline = ActionTimelineClassObj();
    if (timeline == nullptr)
    {
        return 0.0f;
    }
    const NS::ActorAction* action = timeline->FindAction(AirDashActionName());
    // 長さを決めていない動作では割らない。距離は欄の範囲が有限の正の数だけを書く
    if (action == nullptr || action->length <= 0)
    {
        return 0.0f;
    }
    return m_params->m_airDashDistance / (static_cast<float>(action->length) * NS::OS::FrameTimer::FixedDelta());
}

bool PlayerActor::StartAirDashAction() noexcept
{
    // 速さは状態に入った時の動作の長さで決めて控え、途中で読み直しても変えない。IsStateEnd も入った時の長さで終わる
    m_airDash.m_dashSpeed = AirDashSpeed();
    NS::ActionTimeline* timeline = ActionTimelineClassObj();
    if (timeline == nullptr)
    {
        return false;
    }
    // 地形には相手のセンサーが無いので、自分の体のセンサーを両方に渡す。動作が無ければ Start が WARN を出す
    return timeline->Start(ActionTimelineSelfOf(),
                           AirDashActionName(),
                           0,
                           Root().Position(),
                           m_bodyHitSensor,
                           m_bodyHitSensor);
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
    m_airDash.m_reboundElapsed += dt;
    const float lock = m_params->m_reboundLockSeconds;
    // 操作なしの段は、床の上で返っても段の秒まで続ける。返った勢いを見せる長さを当てた所で変えない
    if (m_airDash.m_reboundElapsed <= lock)
    {
        ApplyReboundShift();
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
    const float steerEnd = m_params->m_reboundSteerEndSeconds;
    if (m_airDash.m_reboundElapsed > steerEnd)
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
    // 目標は当たった直後のずらしと同じく画面で倒した向きで、前に倒すと画面の上へ曲がる
    const NS::Vector3 velocity = m_body->Velocity();
    const float speed = velocity.Length();
    if (NS::IsNearlyZero(speed))
    {
        return;
    }
    const NS::Vector3 current = velocity / speed;
    NS::Vector3 target = ReboundShiftInput(current);
    if (NS::IsNearlyZero(target))
    {
        return;
    }
    target.Normalize();
    const float limitRatio = std::min(m_params->m_reboundSteerLimitRatio, std::nextafter(0.5f, 0.0f));
    const float limit = GL::Level::DirectionStepRadians(m_params->m_reboundTableDivisions) * std::max(limitRatio, 0.0f);
    const float span = steerEnd - lock;
    float progress = 1.0f;
    if (span > 0.0f)
    {
        progress = std::clamp((m_airDash.m_reboundElapsed - lock) / span, 0.0f, 1.0f);
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
    BeginTerrainRebound(incoming, normal, baseSpeed);
}

void PlayerActor::BeginTerrainRebound(const NS::Vector3& incoming, const NS::Vector3& normal, float baseSpeed) noexcept
{
    NS::Vector3 assigned{};
    if (!GL::Level::AssignReboundDirection(ReboundTable(), incoming, normal, assigned))
    {
        return;
    }

    // 返る速さは当て方で減らさない。入力が返る向きと合った時だけ ApplyReboundChoice が強める
    const float facing = std::min(-NS::Dot(incoming, normal), 1.0f);
    m_body->SetVelocity(assigned * baseSpeed);

    NS::Vector3 reflected = GL::Level::ReflectAcross(incoming, normal);
    reflected.Normalize();
    m_airDash.m_last = GL::Player::TerrainReboundRecord{.point = Root().Position(),
                                                        .incoming = incoming,
                                                        .normal = normal,
                                                        .reflected = reflected,
                                                        .assigned = assigned,
                                                        .shifted = assigned,
                                                        .facing = facing,
                                                        .speed = baseSpeed};
    m_airDash.m_reboundCount = m_airDash.m_reboundCount + 1;
    m_airDash.m_reboundElapsed = 0.0f;
    m_airDash.m_steeredRadians = 0.0f;
    m_airDash.m_steerStarted = false;
    m_airDash.m_shiftApplied = false;
    StartAirControlReturn();
    (void)m_states->Change<GL::Player::DashReboundPlayerState>();
    // 当たる前から入れていた入力は、当たったフレームに選ぶ。合っていたら強い束で始める
    const GL::Level::ReboundChoice choice = ChooseReboundInput();
    StartTerrainReboundReaction(Root().Position() - normal * m_collider->CapsuleRadius(),
                                facing,
                                choice.result == GL::Level::ReboundInputResult::Matched);
    ApplyReboundChoice(choice);
}

void PlayerActor::StartTerrainReboundReaction(const NS::Vector3& contact, float facing, bool matched) noexcept
{
    m_playerSounds->PlayRebound(this);
    NS::HitReaction* reaction = HitReactionClassObj();
    NS::HitSensor* self = m_bodyHitSensor;
    if (reaction == nullptr || self == nullptr)
    {
        return;
    }
    std::string_view name = "地形の反動[弱い]";
    if (matched || facing >= m_params->m_reboundFullFacing)
    {
        name = "地形の反動[全部]";
    }
    // 前の反動の残像と揺れは、新しい反動の束で始め直す
    StopTackleHitEffects();
    for (const NS::HitOwnerRun& run : StartHitReactionRuns(name, contact, self, self))
    {
        if (run.item.name == "TackleGhost" && Ghost() != nullptr)
        {
            Ghost()->Start(run);
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
    NS::Gfx::DebugDraw::Line(from - hit.incoming * length, from, NS::Color{1.0f, 1.0f, 1.0f, 1.0f});
    NS::Gfx::DebugDraw::Line(from, from + hit.normal * length, NS::Color{0.0f, 0.0f, 1.0f, 1.0f});
    NS::Gfx::DebugDraw::Line(from, from + hit.reflected * length, NS::Color{0.0f, 1.0f, 1.0f, 1.0f});
    NS::Gfx::DebugDraw::Line(from, from + hit.assigned * length, NS::Color{0.0f, 1.0f, 0.0f, 1.0f});
    NS::Gfx::DebugDraw::Line(from, from + hit.shifted * length, NS::Color{1.0f, 0.0f, 1.0f, 1.0f});
    if (!IsDashRebounding())
    {
        return;
    }
    // 黄は今の進む向き (曲げの後)。輪は段の色で、赤が操作なし、黄が補正
    NS::Vector3 now = m_body->Velocity();
    now.Normalize();
    const NS::Vector3 center = Root().Position();
    NS::Gfx::DebugDraw::Line(center, center + now * length, NS::Color{1.0f, 1.0f, 0.0f, 1.0f});
    NS::Color stage{1.0f, 0.0f, 0.0f, 1.0f};
    if (m_airDash.m_steerStarted)
    {
        stage = NS::Color{1.0f, 1.0f, 0.0f, 1.0f};
    }
    const float radius = m_collider->CapsuleRadius();
    NS::Gfx::DebugDraw::Circle(center, NS::Vector3{radius, 0.0f, 0.0f}, NS::Vector3{0.0f, 0.0f, radius}, stage);
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

void PlayerActor::ApplyReboundShift() noexcept
{
    if (m_airDash.m_shiftApplied)
    {
        return;
    }
    ApplyReboundChoice(ChooseReboundInput());
}

GL::Level::ReboundChoice PlayerActor::ChooseReboundInput() const noexcept
{
    const GL::Player::TerrainReboundRecord& hit = m_airDash.m_last;
    if (DesiredSpeedScale() < m_params->StickDeadzone())
    {
        return GL::Level::ReboundChoice{.direction = hit.assigned};
    }
    const GL::Level::ScreenAxes screen = ReboundScreenAxes();
    const NS::Vector3 desired = DesiredDirection();
    const GL::Level::ReboundChoiceLimits limits{
        .screenTieRadians = NS::ToRadians(NS::Degrees{m_params->m_reboundScreenTieDegrees}).value,
        .depthSlackRadians = NS::ToRadians(NS::Degrees{m_params->m_reboundDepthSlackDegrees}).value,
        .stepRadians = GL::Level::DirectionStepRadians(m_params->m_reboundTableDivisions),
        .matchVisibleLength = m_params->m_reboundMatchVisibleLength};
    return GL::Level::ChooseReboundDirection(
        ReboundTable(),
        GL::Level::ReboundHit{.assigned = hit.assigned, .normal = hit.normal, .incoming = hit.incoming},
        screen,
        NS::Dot(desired, screen.right),
        NS::Dot(desired, screen.flatForward),
        limits);
}

void PlayerActor::ApplyReboundChoice(const GL::Level::ReboundChoice& choice) noexcept
{
    if (choice.result == GL::Level::ReboundInputResult::Kept)
    {
        return;
    }
    GL::Player::TerrainReboundRecord& hit = m_airDash.m_last;
    hit.input = choice.result;
    m_airDash.m_shiftApplied = true;
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
        // 割り当てた向きと画面で同じに見える、奥と手前の動きが少ない向きを選んだ時は、その向きへ飛ぶ
        if (!NS::IsNearlyParallel(choice.direction, hit.assigned))
        {
            m_body->SetVelocity(TurnLike(m_body->Velocity(), hit.assigned, choice.direction));
            hit.shifted = choice.direction;
        }
        return;
    }
    m_body->SetVelocity(TurnLike(m_body->Velocity(), hit.assigned, choice.direction));
    hit.shifted = choice.direction;
    StartShiftedReboundReaction();
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
    if (Ghost() != nullptr)
    {
        Ghost()->ScaleOpacity(scale);
    }
    (void)StartHitReaction("地形の反動[ずらし]", Root().Position(), self, self);
}

GL::Level::ScreenAxes PlayerActor::ReboundScreenAxes() const noexcept
{
    const NS::Vector3 flatForward = NS::CalcCameraForwardHorizontal(this);
    const NS::Vector3 right{flatForward.z, 0.0f, -flatForward.x};
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
    if (DesiredSpeedScale() < m_params->StickDeadzone())
    {
        return NS::Vector3{};
    }
    const GL::Level::ScreenAxes screen = ReboundScreenAxes();
    const NS::Vector3 desired = DesiredDirection();
    return GL::Level::ShiftInputOnScreen(assigned,
                                         screen.right,
                                         screen.up,
                                         screen.forward,
                                         NS::Dot(desired, screen.right),
                                         NS::Dot(desired, screen.flatForward));
}
