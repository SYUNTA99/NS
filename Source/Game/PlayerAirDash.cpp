#include "Game/PlayerActor.h"

#include "Game/Level/ReboundDirections.h"
#include "Game/Player/PlayerJudges.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/States/AirDashPlayerState.h"
#include "Game/Player/States/BrakePlayerState.h"
#include "Game/Player/States/DashReboundPlayerState.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/SkidPlayerState.h"
#include "Game/Player/States/WalkPlayerState.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/IUse/IUseCamera.h"
#include "NSlib/Gfx/Render/DebugDraw.h"
#include "NSlib/Phys/JoltCharacter.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

namespace
{
    float AngleBetweenUnit(const NS::Vector3& a, const NS::Vector3& b) noexcept
    {
        return std::acos(std::clamp(NS::Dot(a, b), -1.0f, 1.0f));
    }

    // 2 つが同じか真逆で回す面が決まらない時は v のまま
    NS::Vector3 TurnLike(const NS::Vector3& v, const NS::Vector3& from, const NS::Vector3& to) noexcept
    {
        NS::Vector3 side{to - from * NS::Dot(to, from)};
        if (side.LengthSquared() < NS::k_Epsilon * NS::k_Epsilon)
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

void PlayerActor::BeginAirDash() noexcept
{
    // 押す直前に倒し直した向きより、倒し続けていた向きを優先する
    NS::Vector3 direction = m_airDash.m_intendedDirection;
    const bool stick = direction.LengthSquared() > 0.0f ||
                       (DesiredSpeedScale() >= m_params->StickDeadzone() &&
                        NS::TryNormalizeHorizontal(DesiredDirection(), direction));
    if (!stick && !NS::TryNormalizeHorizontal(m_facingDir, direction))
    {
        direction = NS::CalcCameraForwardHorizontal(this);
    }
    m_airDash.m_direction = direction;
    m_airDash.m_dashElapsed = 0.0f;
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
    m_body->SetVelocity(direction * m_params->AirDashSpeed());
    (void)m_states->Change<GL::Player::AirDashPlayerState>();
}

void PlayerActor::UpdateIntendedDirection(float dt) noexcept
{
    NS::Vector3 tilt{};
    if (DesiredSpeedScale() < m_params->StickDeadzone() || !NS::TryNormalizeHorizontal(DesiredDirection(), tilt))
    {
        m_airDash.m_intendedDirection = NS::Vector3{};
        m_airDash.m_tiltDirection = NS::Vector3{};
        m_airDash.m_tiltSeconds = 0.0f;
        return;
    }
    const float retilt = NS::ToRadians(NS::Degrees{m_params->m_retiltDegrees}).value;
    if (m_airDash.m_tiltDirection.LengthSquared() > 0.0f && AngleBetweenUnit(tilt, m_airDash.m_tiltDirection) <= retilt)
    {
        m_airDash.m_tiltSeconds += dt;
    }
    else
    {
        m_airDash.m_tiltSeconds = 0.0f;
    }
    // ゆっくり回した時は、数え直さずに今の向きへ付いていく
    m_airDash.m_tiltDirection = tilt;
    if (m_airDash.m_tiltSeconds >= m_params->m_intendedHoldSeconds)
    {
        m_airDash.m_intendedDirection = tilt;
    }
}

void PlayerActor::UpdateAirDash(float dt) noexcept
{
    m_airDash.m_dashElapsed += dt;
    if (m_airDash.m_dashElapsed >= m_params->m_airDashSeconds)
    {
        // 当たらずに終わったら、勢いを残して普段の動きへつなぐ
        m_body->SetVelocity(m_airDash.m_direction * m_params->m_airDashExitSpeed);
        ChangeToWalkOrFall();
        return;
    }
    m_body->SetVelocity(m_airDash.m_direction * m_params->AirDashSpeed());
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
    m_body->Decelerate(m_params->m_dashReboundDeceleration, dt);

    // 曲げはスティックの向きだけを目標にする。地形も相手も読まないので、目的地へは向かない
    NS::Vector3 target{};
    NS::Vector3 current{};
    const NS::Vector3 lateral = m_body->LateralVelocity();
    if (DesiredSpeedScale() < m_params->StickDeadzone() ||
        !NS::TryNormalizeHorizontal(DesiredDirection(), target) ||
        !NS::TryNormalizeHorizontal(lateral, current))
    {
        return;
    }
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
    if (!(step > 0.0f))
    {
        return;
    }
    const NS::Vector3 turned = GL::Level::TurnToward(current, target, step);
    m_airDash.m_steeredRadians += AngleBetweenUnit(current, turned);
    m_body->SetLateralVelocity(turned * lateral.Length());
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
    if (!(speed > NS::k_Epsilon))
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
    if (!dashing && NS::Phys::IsWalkableNormal(normal.y))
    {
        return;
    }
    float baseSpeed = speed;
    if (dashing)
    {
        baseSpeed = m_params->AirDashSpeed() * m_params->m_terrainReboundSpeedScale;
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

    const float facing = std::min(-NS::Dot(incoming, normal), 1.0f);
    const float keep = std::clamp(m_params->m_reboundKeepRatio, 0.0f, 1.0f);
    const float speed = baseSpeed * (keep + (1.0f - keep) * facing);
    m_body->SetVelocity(assigned * speed);

    NS::Vector3 reflected = GL::Level::ReflectAcross(incoming, normal);
    reflected.Normalize();
    m_airDash.m_last = GL::Player::TerrainReboundRecord{.point = Root().Position(),
                                                        .incoming = incoming,
                                                        .normal = normal,
                                                        .reflected = reflected,
                                                        .assigned = assigned,
                                                        .shifted = assigned,
                                                        .facing = facing,
                                                        .speed = speed};
    m_airDash.m_reboundCount = m_airDash.m_reboundCount + 1;
    m_airDash.m_reboundElapsed = 0.0f;
    m_airDash.m_steeredRadians = 0.0f;
    m_airDash.m_steerStarted = false;
    m_airDash.m_shiftApplied = false;
    StartAirControlReturn();
    (void)m_states->Change<GL::Player::DashReboundPlayerState>();
    StartTerrainReboundReaction(Root().Position() - normal * m_collider->CapsuleRadius(), facing);
}

void PlayerActor::StartTerrainReboundReaction(const NS::Vector3& contact, float facing) noexcept
{
    NS::HitReaction* reaction = HitReactionClassObj();
    NS::HitSensor* self = BodySensorClassObj();
    if (reaction == nullptr || self == nullptr)
    {
        return;
    }
    std::string_view name = "地形の反動[弱い]";
    if (facing >= m_params->m_reboundFullFacing)
    {
        name = "地形の反動[全部]";
    }
    // 前の反動の残像と揺れは、新しい反動の束で始め直す
    StopTackleHitEffects();
    // 束が足した持ち主へ渡す項目は、走っている項目の後ろに並ぶ
    const std::size_t first = reaction->OwnerRuns().size();
    (void)StartHitReaction(name, contact, self, self);
    const std::vector<NS::HitOwnerRun>& runs = reaction->OwnerRuns();
    for (std::size_t i = first; i < runs.size(); ++i)
    {
        if (runs[i].item.name == "TackleGhost")
        {
            m_ghostRun.Start(runs[i], this);
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
    const GL::Player::TerrainReboundRecord& hit = m_airDash.m_last;
    const NS::Vector3 shifted =
        GL::Level::ShiftReboundDirection(ReboundTable(),
                                         hit.assigned,
                                         hit.normal,
                                         ReboundShiftInput(hit.assigned),
                                         GL::Level::DirectionStepRadians(m_params->m_reboundTableDivisions));
    if (AngleBetweenUnit(shifted, hit.assigned) < NS::k_Epsilon)
    {
        return;
    }
    m_body->SetVelocity(TurnLike(m_body->Velocity(), hit.assigned, shifted));
    m_airDash.m_last.shifted = shifted;
    m_airDash.m_shiftApplied = true;
}

NS::Vector3 PlayerActor::ReboundShiftInput(const NS::Vector3& assigned) const noexcept
{
    if (DesiredSpeedScale() < m_params->StickDeadzone())
    {
        return NS::Vector3{};
    }
    const NS::Vector3 flatForward = NS::CalcCameraForwardHorizontal(this);
    const NS::Vector3 right{flatForward.z, 0.0f, -flatForward.x};
    NS::Vector3 forward = flatForward;
    const std::optional<NS::CameraPose> pose = NS::GetCameraViewPose(this);
    if (pose.has_value())
    {
        const NS::Vector3 look = pose->target - pose->position;
        if (look.LengthSquared() > NS::k_Epsilon * NS::k_Epsilon)
        {
            forward = look;
            forward.Normalize();
        }
    }
    NS::Vector3 up = NS::Cross(forward, right);
    up.Normalize();
    const NS::Vector3 desired = DesiredDirection();
    return GL::Level::ShiftInputOnScreen(
        assigned, right, up, forward, NS::Dot(desired, right), NS::Dot(desired, flatForward));
}
