#include "Game/PlayerActor.h"

#include "Game/Level/AirGravity.h"
#include "Game/Level/ImpactOutcome.h"
#include "Game/Player/PlayerAcceleration.h"
#include "Game/Player/PlayerJudges.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/LedgeClimbingPlayerState.h"
#include "Game/Player/States/LedgeHangingPlayerState.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/Phys/JoltCharacter.h"

#include <algorithm>
#include <cmath>

namespace
{
    // 水平の向き from を Y 軸まわりに radians だけ回す。正の角度は +X を -Z の側へ回す
    NS::Vector3 RotateHorizontal(const NS::Vector3& from, float radians) noexcept
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        return NS::Vector3{from.x * c + from.z * s, 0.0f, -from.x * s + from.z * c};
    }

    // from を RotateHorizontal で回して to へ重ねる角度。範囲は -π..π
    float HorizontalAngleBetween(const NS::Vector3& from, const NS::Vector3& to) noexcept
    {
        return std::atan2(from.z * to.x - from.x * to.z, from.x * to.x + from.z * to.z);
    }

    // from を Y 軸まわりに最大 maxRadians だけ to へ寄せた向き。from と to は正規化した水平の向き。
    NS::Vector3 TurnHorizontalToward(const NS::Vector3& from, const NS::Vector3& to, float maxRadians) noexcept
    {
        const float angle = HorizontalAngleBetween(from, to);
        if (std::abs(angle) <= maxRadians)
        {
            return to;
        }
        float step = maxRadians;
        if (angle < 0.0f)
        {
            step = -maxRadians;
        }
        return RotateHorizontal(from, step);
    }
} // namespace

void PlayerActor::MoveBody(float dt) noexcept
{
    // 壁に当たった後の速度からは面へ向かう分が抜ける。掴む向きに使うので、抜ける前の向きを覚える
    NS::Vector3 target{};
    if (NS::TryNormalizeHorizontal(m_body->LateralVelocity(), target))
    {
        NS::Vector3 current{};
        if (m_params->m_turnSpeed > 0.0f && NS::TryNormalizeHorizontal(m_facingDir, current))
        {
            const float maxTurn = NS::ToRadians(NS::Degrees{m_params->m_turnSpeed * dt}).value;
            m_facingDir = TurnHorizontalToward(current, target, maxTurn);
        }
        else
        {
            m_facingDir = target;
        }
    }

    const NS::Vector3 before = Root().Position();
    m_airDash.m_intended = m_body->Velocity();
    m_body->Move(&Root(), m_collider.get(), dt, m_params->m_maxStepHeight);
    const NS::Vector3 delta = Root().Position() - before;
    m_lastMoveDistance = delta.Length();
    SyncGroundState();
    AdvanceBodySlamTravel(delta);
    m_slam.m_wasSlamming = IsBodySlamming();
    TryBeginTerrainRebound();
}

void PlayerActor::SyncGroundState() noexcept
{
    if (!m_body->IsGrounded())
    {
        return;
    }
    if (!m_body->WasGrounded())
    {
        RestoreOnLanding();
    }
    else
    {
        RestoreWhileGrounded();
    }
}

void PlayerActor::RestoreOnLanding() noexcept
{
    m_jumpsRemaining = 1;
    m_airControlReturning = false;
    RestoreWhileGrounded();
}

void PlayerActor::RestoreWhileGrounded() noexcept
{
    m_coyoteTimer = m_params->m_coyoteTime;
    // 着地のフレームだけで戻すと、接地したまま走り抜けた突進の後に次が出せない
    m_request.spent = false;
    m_airDash.m_remaining = 1;
}

void PlayerActor::TickTimers(float dt) noexcept
{
    NS::Body& body = *m_body;
    m_bufferTimer -= dt;
    if (m_input->JumpPressed())
    {
        m_bufferTimer = m_params->m_jumpBufferTime;
    }

    const bool inAir = !body.IsGrounded();
    if (inAir)
    {
        m_coyoteTimer -= dt;
    }
}

bool PlayerActor::TryMoveInput(NS::Vector3& outDirection, float& outTopSpeed) const noexcept
{
    if (!GL::Player::PlayerJudgeMoveInput::Judge(DesiredSpeedScale(), m_params->StickDeadzone()) ||
        !NS::TryNormalizeHorizontal(DesiredDirection(), outDirection))
    {
        return false;
    }
    outTopSpeed = std::max(MaxSpeed() * DesiredSpeedScale(), m_params->m_walkSpeed);
    return true;
}

void PlayerActor::AccelerateToInputDirection(float dt) noexcept
{
    NS::Body& body = *m_body;
    NS::Vector3 direction{};
    float topSpeed = 0.0f;
    if (!TryMoveInput(direction, topSpeed))
    {
        return;
    }

    float acceleration = m_params->m_airAcceleration;
    float turningDrag = m_params->m_turningDrag;
    if (body.IsGrounded())
    {
        acceleration = m_params->m_acceleration;
    }
    else if (m_airControlReturning)
    {
        // 返った軌道を落ちた最初のフレームで折らない。0 から滑らかに上げ、戻りの初めほどゆっくり効かせる
        const float seconds = m_params->m_airControlReturnSeconds;
        float t = 1.0f;
        if (seconds > 0.0f)
        {
            t = std::clamp(m_airControlReturnElapsed / seconds, 0.0f, 1.0f);
        }
        const float scale = t * t * (3.0f - 2.0f * t);
        acceleration *= scale;
        turningDrag *= scale;
    }
    GL::Player::Accelerate(&body, direction, turningDrag, acceleration, topSpeed, dt);
}

void PlayerActor::StartAirControlReturn() noexcept
{
    m_airControlReturning = true;
    m_airControlReturnElapsed = 0.0f;
}

void PlayerActor::AdvanceAirControlReturn(float dt) noexcept
{
    if (!m_airControlReturning)
    {
        return;
    }
    m_airControlReturnElapsed += dt;
    if (m_airControlReturnElapsed >= m_params->m_airControlReturnSeconds)
    {
        m_airControlReturning = false;
    }
}

void PlayerActor::ApplyFriction(float dt) noexcept
{
    GL::Player::Decelerate(m_body.get(), m_params->m_friction, dt);
}

void PlayerActor::ApplyBrake(float dt) noexcept
{
    GL::Player::Decelerate(m_body.get(), m_params->m_deceleration, dt);
}

void PlayerActor::Jump(float) noexcept
{
    NS::Body& body = *m_body;
    if (GL::Player::PlayerJudgeJump::Judge(
            body.IsGrounded(), m_coyoteTimer, m_jumpsRemaining, m_input->JumpPressed(), m_bufferTimer))
    {
        LaunchJump();
    }
}

void PlayerActor::LaunchJump() noexcept
{
    m_body->SetVerticalVelocity(m_params->m_jumpImpulse);
    // 縁からは跳べる回数を見ずに跳ぶので、0 から先へは減らさない
    m_jumpsRemaining = std::max(0, m_jumpsRemaining - 1);
    m_bufferTimer = 0.0f;
    m_coyoteTimer = 0.0f;
    m_playerEvents.onJump.Invoke();
}

void PlayerActor::CutJumpRelease() noexcept
{
    NS::Body& body = *m_body;
    if (m_prevJumpHeld && !m_input->JumpHeld() && body.VerticalVelocity() > 0.0f)
    {
        body.SetVerticalVelocity(body.VerticalVelocity() * m_params->m_jumpReleaseScale);
    }
}

void PlayerActor::Gravity(float dt) noexcept
{
    // 強さの選び方は LaunchPitch と同じ関数。写すと欄を変えた時に放つ角度の予測と実際の落ち方が割れる
    NS::Body& body = *m_body;
    body.Gravity(GL::Level::ChooseGravity(m_params->Gravity(), body.VerticalVelocity()), dt);
}

void PlayerActor::TapSlamGravity(float dt) noexcept
{
    // 進み切る前に着地すると残りを地面の上で滑り、走っていないのに動いて見える
    // 滞空秒を踏み込みの秒へ合わせ、進み切った所で足が着くようにする
    float airSeconds = 0.0f;
    if (m_params->m_tapSlamSpeed > 0.0f)
    {
        airSeconds = m_params->m_tapSlamDistance / m_params->m_tapSlamSpeed;
    }
    // Inspector で 0 を置くと 0 除算で位置まで非有限値が伝わるため、距離か初速が 0 なら通常の重力へ戻す
    if (!NS::HasPositiveMagnitude(airSeconds))
    {
        Gravity(dt);
        return;
    }

    // 上下対称の弧なので、山の高さは tapSlamUpSpeed * airSeconds / 4 で決まる
    // 高さを変えたい時に触るのは tapSlamUpSpeed で、ここは触らない
    const float g = -2.0f * m_params->m_tapSlamUpSpeed / airSeconds;
    m_body->Gravity(g, dt);
}

void PlayerActor::ReboundGravity(float dt) noexcept
{
    // 上りだけ倍率付きの組を PlayerParams が持つ。選び方は普段の重力と同じ ChooseGravity 1 つ
    NS::Body& body = *m_body;
    body.Gravity(GL::Level::ChooseGravity(m_params->ReboundGravity(), body.VerticalVelocity()), dt);
}

void PlayerActor::AccelerateDuringRebound(float dt) noexcept
{
    NS::Body& body = *m_body;
    NS::Vector3 direction{};
    float topSpeed = 0.0f;
    if (!TryMoveInput(direction, topSpeed))
    {
        return;
    }

    // 横入力で反動方向を消さないよう、入力からずれた速度は削らない
    const float turningDrag = 0.0f;
    // 明けのフレームは止める前の接地のフラグが残っている。接地を見て地上の加速度を選ぶと、そのフレームだけ大きく曲がる
    GL::Player::Accelerate(&body, direction, turningDrag, m_params->m_reboundAirAcceleration, topSpeed, dt);
}

void PlayerActor::BeginSkid() noexcept
{
    m_skid.m_landingVelocity = m_body->LateralVelocity();
    m_skid.m_elapsedSteps = 0;
    m_skid.m_speedScale = GL::Level::MissSkidSpeedScale(0, m_params->m_missSkidSteps, m_params->m_missSkidExponent);
}

bool PlayerActor::AdvanceSkid() noexcept
{
    ++m_skid.m_elapsedSteps;
    // 着いた速さに依らず同じフレーム数で止まりきり、操作が戻る
    m_skid.m_speedScale =
        GL::Level::MissSkidSpeedScale(m_skid.m_elapsedSteps, m_params->m_missSkidSteps, m_params->m_missSkidExponent);
    m_body->SetLateralVelocity(m_skid.m_landingVelocity * m_skid.m_speedScale);
    return m_skid.m_elapsedSteps >= m_params->m_missSkidSteps;
}

void PlayerActor::UpdateBodySlam(float dt) noexcept
{
    NS::Body& body = *m_body;
    // 突進中に向きを変えられると当てる間合いを詰める意味が消えるので、水平は発動時の値で書き直す
    // 書き手はここだけで、壁に押し付けられて潰れた水平も次のフレームで戻る
    if (!m_slam.m_isTap)
    {
        body.SetLateralVelocity(BodySlamVelocity());
    }

    if (m_slam.m_isTap)
    {
        TapSlamGravity(dt);
    }
    else
    {
        Gravity(dt);
    }
}

namespace
{
    // 掴む縁の 1 か所
    struct LedgeSpot
    {
        NS::Vector3 hangPos;    // ぶら下がりの体の中心
        NS::Vector3 faceNormal; // 掴む面の、自機の側を向く水平の向き
        float topY = 0.0f;      // 縁の上面の高さ
    };

    // よじ登りを終えた体の中心。塞がっているかの確かめと登る先の両方がこの点を使う
    // ぶら下がりの中心は面から半径ぶん外。直径ぶん奥へ進めると中心が縁から半径ぶん内側に入り、体が上面に乗る
    // halfHeight は円柱の半分の高さで、掴む時は立ち姿、掴まった後は今の当たりの形で測る
    [[nodiscard]] NS::Vector3 MantleEnd(const NS::Collider& collider, float halfHeight, const LedgeSpot& ledge) noexcept
    {
        const float mantleStep = 2.0f * collider.CapsuleRadius();
        return NS::Vector3{
            ledge.hangPos.x - ledge.faceNormal.x * mantleStep,
            ledge.topY + halfHeight + collider.CapsuleRadius(),
            ledge.hangPos.z - ledge.faceNormal.z * mantleStep,
        };
    }

    // 縁の上面を探す手の高さの帯
    struct LedgeBand
    {
        float handY = 0.0f; // 手の高さ
        float below = 0.0f; // 手より下に許す上面の深さ
        float above = 0.0f; // 手より上に許す上面の高さ
    };

    // 縁を当たりの本当の形で探す。境界箱で探すと、傾いた坂や回した壁の周りの空中に縁ができる。
    // 壁の面は、上面が帯に入る壁なら必ず当たる帯の一番下の高さで、dir へ撃って見つける。
    // 上面は、面から reach だけ奥を帯の上から下へ撃って見つける。登れる傾きの面は壁にも上面にもしない
    [[nodiscard]] bool FindLedge(const NS::Collider& collider,
                                 const NS::Vector3& center,
                                 const NS::Vector3& dir,
                                 const LedgeBand& band,
                                 float reach,
                                 float halfHeight,
                                 LedgeSpot& outLedge)
    {
        const float radius = collider.CapsuleRadius();
        const NS::Vector3 faceOrigin{center.x, band.handY - band.below, center.z};
        float faceDistance = 0.0f;
        NS::Vector3 faceHitNormal{};
        if (!NS::RaycastCollision(&collider, faceOrigin, dir, radius + reach, faceDistance, faceHitNormal, JPH::BodyID{}) ||
            NS::Phys::IsWalkableNormal(faceHitNormal.y))
        {
            return false;
        }
        NS::Vector3 faceNormal{};
        if (!NS::TryNormalizeHorizontal(faceHitNormal, faceNormal))
        {
            return false;
        }

        const NS::Vector3 facePoint = faceOrigin + dir * faceDistance;
        const NS::Vector3 topOrigin{facePoint.x - faceNormal.x * reach,
                                    band.handY + band.above + radius,
                                    facePoint.z - faceNormal.z * reach};
        float topDistance = 0.0f;
        NS::Vector3 topNormal{};
        if (!NS::RaycastCollision(&collider,
                                  topOrigin,
                                  NS::Vector3{0.0f, -1.0f, 0.0f},
                                  band.above + band.below + 2.0f * radius,
                                  topDistance,
                                  topNormal,
                                  JPH::BodyID{}) ||
            !NS::Phys::IsWalkableNormal(topNormal.y))
        {
            return false;
        }
        const float top = topOrigin.y - topDistance;
        if (!GL::Player::PlayerJudgeLedgeGrab::InBand(band.handY, top, band.below, band.above))
        {
            return false;
        }

        const LedgeSpot ledge{
            NS::Vector3{facePoint.x + faceNormal.x * radius, top - halfHeight, facePoint.z + faceNormal.z * radius},
            faceNormal,
            top,
        };
        // 登り終えた体が地形に入るなら縁ではない。オーバーハングと低い天井の下では掴めない。
        // 体と同じ太さで、上面から半径だけ上から頭の上端までを見る。足元は上面に触れるので見ない
        const NS::Vector3 mantleEnd = MantleEnd(collider, halfHeight, ledge);
        const float headTop = mantleEnd.y + halfHeight + radius;
        const float lowestCenter = top + 2.0f * radius;
        const float highestCenter = std::max(lowestCenter, headTop - radius);
        const NS::Phys::Capsule climbed{
            .center = NS::Vector3{mantleEnd.x, 0.5f * (lowestCenter + highestCenter), mantleEnd.z},
            .halfHeight = 0.5f * (highestCenter - lowestCenter),
            .radius = radius,
        };
        if (NS::OverlapsCapsuleCollision(&collider, climbed))
        {
            return false;
        }
        outLedge = ledge;
        return true;
    }
} // namespace

bool PlayerActor::LedgeGrab() noexcept
{
    NS::Body& body = *m_body;
    if (!GL::Player::PlayerJudgeLedgeGrab::Judge(body.IsGrounded(), body.VerticalVelocity(), m_slam.m_wasSlamming))
    {
        return false;
    }

    NS::Vector3 dir{};
    if (!NS::TryNormalizeHorizontal(m_facingDir, dir))
    {
        return false;
    }

    // 縁は立ち姿の手の高さで測る。玉の寸法では低すぎる所を探し、落下中に掴めない
    const NS::Collider& collider = *m_collider;
    const float halfHeight = collider.StandingHalfHeight();
    NS::Vector3 pos = Root().Position();
    pos.y += halfHeight - collider.CapsuleHalfHeight();
    // 帯の上は今フレーム動いた距離まで。速く落ちると 1 フレームで縁の上端を通り過ぎて掴み損ねる
    const LedgeBand band{pos.y + halfHeight, m_params->m_ledgeGrabBelowHand, m_lastMoveDistance};
    LedgeSpot ledge{};
    if (!FindLedge(collider, pos, dir, band, m_params->m_ledgeReach, halfHeight, ledge))
    {
        return false;
    }

    // 掴まりからは突進が出ないので、立ち姿でぶら下がる。ぶら下がる位置は立ち姿の中心なので、
    // 置く前に解く。置いた後に解くと根が半長ぶん上がる
    ChangeCurled(false);
    Root().SetPosition(ledge.hangPos);
    body.SetVelocity(NS::Vector3{0.0f, 0.0f, 0.0f});
    m_ledgeTopY = ledge.topY;
    m_ledgeFaceNormal = ledge.faceNormal;
    (void)m_states->Change<GL::Player::LedgeHangingPlayerState>();
    m_playerEvents.onLedgeGrabbed.Invoke();
    return true;
}

bool PlayerActor::HoldLedge() noexcept
{
    NS::Body& body = *m_body;
    float top = 0.0f;
    NS::Vector3 faceNormal{};
    NS::Vector3 hangPos{};
    if (!FindLedgeTopAt(Root().Position(), top, faceNormal, hangPos))
    {
        DropLedge();
        return false;
    }

    m_ledgeTopY = top;
    m_ledgeFaceNormal = faceNormal;
    m_facingDir = -faceNormal;
    Root().SetPosition(hangPos);
    body.SetVelocity(NS::Vector3{0.0f, 0.0f, 0.0f});
    return true;
}

bool PlayerActor::LedgeJump() noexcept
{
    if (!m_input->JumpPressed())
    {
        return false;
    }

    LaunchJump();
    m_body->SetGrounded(false);
    (void)m_states->Change<GL::Player::FallPlayerState>();
    return true;
}

void PlayerActor::ClimbLedge() noexcept
{
    NS::Body& body = *m_body;
    const NS::Collider& collider = *m_collider;
    const NS::Vector3 pos = Root().Position();
    m_ledgeMantleStart = pos;
    m_ledgeMantleEnd = MantleEnd(collider, collider.CapsuleHalfHeight(), LedgeSpot{pos, m_ledgeFaceNormal, m_ledgeTopY});
    m_ledgeMantleTimer = 0.0f;
    (void)m_states->Change<GL::Player::LedgeClimbingPlayerState>();
    body.SetVelocity(NS::Vector3{0.0f, 0.0f, 0.0f});
    m_playerEvents.onLedgeClimbing.Invoke();
}

void PlayerActor::DropLedge() noexcept
{
    NS::Body& body = *m_body;
    (void)m_states->Change<GL::Player::FallPlayerState>();
    body.SetVelocity(NS::Vector3{0.0f, 0.0f, 0.0f});
    body.SetGrounded(false);
    // 壁と逆を向いて落ちる。壁を向いたままだと、帯の上の余白に縁が入って次のフレームで掴み直す
    m_facingDir = m_ledgeFaceNormal;
}

void PlayerActor::Shimmy(float dt) noexcept
{
    if (ClimbRight() != 0.0f)
    {
        // 面法線に水平直交する縁方向へ進め、移動先で見つけた面から半径だけ外と上面の高さへ置き直す。面が折れていても面に沿う
        const NS::Vector3 pos = Root().Position();
        const NS::Vector3 alongDir{-m_ledgeFaceNormal.z, 0.0f, m_ledgeFaceNormal.x};
        const NS::Vector3 step = alongDir * (ClimbRight() * m_params->m_ledgeShimmySpeed * dt);
        const NS::Vector3 shimmied = pos + step;
        // 移動先にも掴める縁が続いている時だけ動く。端なら止めて落とさない
        float top = 0.0f;
        NS::Vector3 faceNormal{};
        NS::Vector3 hangPos{};
        if (FindLedgeTopAt(shimmied, top, faceNormal, hangPos))
        {
            // 面が手前へ折れている所で見つけた面の前へ置くと、面に沿って進んだ向きの逆へ跳ぶ。
            // その時は見つけた面に垂直な向きだけ寄せる。奥へ折れている所は面の前へ置き、角を回り込む
            NS::Vector3 placed = hangPos;
            if (faceNormal.x * step.x + faceNormal.z * step.z < 0.0f)
            {
                const NS::Vector3 offset = shimmied - hangPos;
                const float awayFromFace = offset.x * faceNormal.x + offset.z * faceNormal.z;
                placed = NS::Vector3{
                    shimmied.x - faceNormal.x * awayFromFace, hangPos.y, shimmied.z - faceNormal.z * awayFromFace};
            }
            // 折れた面へ持ち替えたら、向きも上面の高さも新しい面に合わせる
            m_ledgeFaceNormal = faceNormal;
            m_ledgeTopY = top;
            m_facingDir = -faceNormal;
            Root().SetPosition(placed);
        }
    }
}

void PlayerActor::UpdateLedgeClimb(float dt) noexcept
{
    NS::Body& body = *m_body;
    m_ledgeMantleTimer += dt;
    float t = 1.0f;
    if (m_params->m_ledgeClimbDuration > 0.0f)
    {
        t = NS::Clamp(m_ledgeMantleTimer / m_params->m_ledgeClimbDuration, 0.0f, 1.0f);
    }

    // 2 段に割るのは角への食い込みを避けるため。前半は上昇だけで前へ進まない
    NS::Vector3 pos{0.0f, 0.0f, 0.0f};
    if (t < 0.5f)
    {
        const float u = t / 0.5f;
        pos.x = m_ledgeMantleStart.x;
        pos.z = m_ledgeMantleStart.z;
        pos.y = NS::Lerp(m_ledgeMantleStart.y, m_ledgeMantleEnd.y, u);
    }
    else
    {
        const float u = (t - 0.5f) / 0.5f;
        pos.x = NS::Lerp(m_ledgeMantleStart.x, m_ledgeMantleEnd.x, u);
        pos.z = NS::Lerp(m_ledgeMantleStart.z, m_ledgeMantleEnd.z, u);
        pos.y = m_ledgeMantleEnd.y;
    }
    Root().SetPosition(pos);
    body.SetVelocity(NS::Vector3{0.0f, 0.0f, 0.0f});

    if (t >= 1.0f)
    {
        Root().SetPosition(m_ledgeMantleEnd);
        (void)m_states->Change<GL::Player::IdlePlayerState>();
        body.SetGrounded(true);
        // SetGrounded は前の接地も真にするので、次の移動は着地と読まない。ここで着地の分を戻す
        RestoreOnLanding();
    }
}

bool PlayerActor::FindLedgeTopAt(const NS::Vector3& hangPos,
                                 float& outTop,
                                 NS::Vector3& outFaceNormal,
                                 NS::Vector3& outHangPos) const noexcept
{
    const NS::Collider& collider = *m_collider;
    const NS::Vector3 inward{-m_ledgeFaceNormal.x, 0.0f, -m_ledgeFaceNormal.z};
    // 手の高さは前のフレームに測った上面。撃った線で測り直す上面は誤差で手より少し上に出るので、その分を許す
    const LedgeBand band{
        hangPos.y + collider.CapsuleHalfHeight(), m_params->m_ledgeGrabBelowHand, m_params->m_ledgeHoldAboveHand};
    LedgeSpot ledge{};
    if (!FindLedge(collider, hangPos, inward, band, m_params->m_ledgeReach, collider.CapsuleHalfHeight(), ledge))
    {
        return false;
    }
    outTop = ledge.topY;
    outFaceNormal = ledge.faceNormal;
    outHangPos = ledge.hangPos;
    return true;
}
