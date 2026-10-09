#include "Game/Player.h"

#include "Game/Level/ImpactOutcome.h"
#include "Game/Player/PlayerGravity.h"
#include "Game/Player/PlayerJudges.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/LedgeClimbingPlayerState.h"
#include "Game/Player/States/LedgeHangingPlayerState.h"
#include "NSlib/Object/SubObjects/Body.h"
#include "NSlib/Object/SubObjects/Collider.h"
#include "NSlib/Object/SubObjects/PlayerInput.h"

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
    // 使うのは MoveBody の振り向きだけなので、ここに置く
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

// ---- 移動の組み立て ----
// 状態の OnStep と、身体の段 (BodyStep) から呼ぶ MoveBody が、ここの関数を呼ぶ。
// 身体 (Body) の速度・接地・重力の計算を呼ぶだけで、値は PlayerParams から読む

void Player::MoveBody(float dt) noexcept
{
    // 壁に当たった後の速度からは面へ向かう分が抜ける。掴む向きに使うので、抜ける前の向きを覚える
    NS::Vector3 target{};
    if (NS::TryNormalizeHorizontal(m_body->LateralVelocity(), target))
    {
        // 一定の速さで回す。速さの理由は m_turnSpeed の欄
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
    m_body->Move(*m_collider, dt, m_params->m_maxStepHeight);
    const NS::Vector3 delta = Root().Position() - before;
    m_lastMoveDistance = delta.Length();
    SyncGroundState();
    AdvanceBodySlamTravel(delta);
    m_slam.wasSlamming = IsBodySlamming();
}

void Player::SyncGroundState() noexcept
{
    if (!m_body->WasGrounded() && m_body->IsGrounded())
    {
        m_jumpsRemaining = 1;
    }

    if (m_body->IsGrounded())
    {
        m_coyoteTimer = m_params->m_coyoteTime;
        // 着地のフレームだけで戻すと、接地したまま走り抜けた突進の後に次が出せない
        m_request.spent = false;
    }
}

void Player::TickTimers(float dt) noexcept
{
    NS::Obj::Body& body = *m_body;
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

bool Player::TryMoveInput(NS::Vector3& outDirection, float& outTopSpeed) const noexcept
{
    if (!GL::Player::PlayerJudgeMoveInput::Judge(DesiredSpeedScale(), m_params->StickDeadzone()) ||
        !NS::TryNormalizeHorizontal(DesiredDirection(), outDirection))
    {
        return false;
    }
    outTopSpeed = std::max(MaxSpeed() * DesiredSpeedScale(), m_params->m_walkSpeed);
    return true;
}

void Player::AccelerateToInputDirection(float dt) noexcept
{
    NS::Obj::Body& body = *m_body;
    NS::Vector3 direction{};
    float topSpeed = 0.0f;
    if (!TryMoveInput(direction, topSpeed))
    {
        return;
    }

    float acceleration = m_params->m_airAcceleration;
    if (body.IsGrounded())
    {
        acceleration = m_params->m_acceleration;
    }
    body.Accelerate(direction, m_params->m_turningDrag, acceleration, topSpeed, dt);
}

void Player::ApplyFriction(float dt) noexcept
{
    m_body->Decelerate(m_params->m_friction, dt);
}

void Player::ApplyBrake(float dt) noexcept
{
    m_body->Decelerate(m_params->m_deceleration, dt);
}

void Player::Jump(float) noexcept
{
    NS::Obj::Body& body = *m_body;
    if (GL::Player::PlayerJudgeJump::Judge(
            body.IsGrounded(), m_coyoteTimer, m_jumpsRemaining, m_input->JumpPressed(), m_bufferTimer))
    {
        body.SetVerticalVelocity(m_params->m_jumpImpulse);
        --m_jumpsRemaining;
        m_bufferTimer = 0.0f;
        m_coyoteTimer = 0.0f;
        m_playerEvents.onJump.Invoke();
    }
}

void Player::CutJumpRelease() noexcept
{
    NS::Obj::Body& body = *m_body;
    if (m_prevJumpHeld && !m_input->JumpHeld() && body.VerticalVelocity() > 0.0f)
    {
        body.SetVerticalVelocity(body.VerticalVelocity() * m_params->m_jumpReleaseScale);
    }
}

void Player::Gravity(float dt) noexcept
{
    // 強さの選び方は LaunchPitch と同じ関数。写すと欄を変えた時に放つ角度の予測と実際の落ち方が割れる
    NS::Obj::Body& body = *m_body;
    body.Gravity(GL::Player::ChooseGravity(m_params->Gravity(), body.VerticalVelocity()), dt);
}

void Player::TapSlamGravity(float dt) noexcept
{
    // 進み切る前に着地すると残りを地面の上で滑り、走っていないのに動いて見える
    // 滞空秒を踏み込みの秒へ合わせ、進み切った所で足が着くようにする
    float airSeconds = 0.0f;
    if (m_params->m_tapSlamSpeed > 0.0f)
    {
        airSeconds = m_params->m_tapSlamDistance / m_params->m_tapSlamSpeed;
    }
    // Inspector で 0 を置くと 0 除算で位置まで非有限値が伝わるため、距離か初速が 0 なら通常の重力へ戻す
    if (!(airSeconds > NS::k_Epsilon))
    {
        Gravity(dt);
        return;
    }

    // 上下対称の弧なので、山の高さは tapSlamUpSpeed * airSeconds / 4 で決まる
    // 高さを変えたい時に触るのは tapSlamUpSpeed で、ここは触らない
    const float g = -2.0f * m_params->m_tapSlamUpSpeed / airSeconds;
    m_body->Gravity(g, dt);
}

void Player::ReboundGravity(float dt) noexcept
{
    // 上りだけ倍率付きの組を PlayerParams が持つ。選び方は普段の重力と同じ ChooseGravity 1 つ
    NS::Obj::Body& body = *m_body;
    body.Gravity(GL::Player::ChooseGravity(m_params->ReboundGravity(), body.VerticalVelocity()), dt);
}

void Player::AccelerateDuringRebound(float dt) noexcept
{
    NS::Obj::Body& body = *m_body;
    NS::Vector3 direction{};
    float topSpeed = 0.0f;
    if (!TryMoveInput(direction, topSpeed))
    {
        return;
    }

    // 入力の向きからずれた速度は削らない。削ると横へ倒しただけで相手から離れる流れが消え、
    // 弾かれる向きが当て方でなくスティックで決まる。触って詰める値ではないので欄にしない
    const float turningDrag = 0.0f;
    // 明けのフレームは止める前の接地の印が残っている。接地を見て地上の加速度を選ぶと、そのフレームだけ大きく曲がる
    body.Accelerate(direction, turningDrag, m_params->m_reboundAirAcceleration, topSpeed, dt);
}

void Player::BeginSkid() noexcept
{
    m_skid.landingVelocity = m_body->LateralVelocity();
    m_skid.elapsedSteps = 0;
}

bool Player::AdvanceSkid() noexcept
{
    ++m_skid.elapsedSteps;
    // 着いた速さに依らず同じフレーム数で止まりきり、操作が戻る
    const float scale = GL::Level::MissSkidSpeedScale(
        m_skid.elapsedSteps, m_params->m_missSkidSteps, m_params->m_missSkidExponent);
    m_body->SetLateralVelocity(m_skid.landingVelocity * scale);
    return m_skid.elapsedSteps >= m_params->m_missSkidSteps;
}

void Player::UpdateBodySlam(float dt) noexcept
{
    NS::Obj::Body& body = *m_body;
    // 突進中に向きを変えられると当てる間合いを詰める意味が消えるので、水平は発動時の値で書き直す
    // 書き手はここだけで、壁に押し付けられて潰れた水平も次のフレームで戻る
    if (!m_slam.isTap)
    {
        body.SetLateralVelocity(BodySlamVelocity());
    }

    if (m_slam.isTap)
    {
        TapSlamGravity(dt);
    }
    else
    {
        Gravity(dt);
    }
}

// ---- 崖つかまり ----
// 縁の検出・つかまり・登り

namespace
{
    // 掴まりの走査で見る AABB 群。体が Scene に居なければ空で、掴めないだけ
    [[nodiscard]] std::vector<NS::AABB> BoxesTouchingBand(const NS::Obj::IUseCollision& collider,
                                                          const NS::Vector3& probe,
                                                          float below,
                                                          float above)
    {
        NS::AABB region;
        region.Center = NS::Vector3{probe.x, probe.y + 0.5f * (above - below), probe.z};
        region.Extents = NS::Vector3{0.0f, 0.5f * (above + below), 0.0f};
        return NS::Obj::OverlapBoxCollision(collider, region);
    }

    [[nodiscard]] std::vector<NS::AABB> BoxesAtPoint(const NS::Obj::IUseCollision& collider, const NS::Vector3& point)
    {
        NS::AABB region;
        region.Center = point;
        region.Extents = NS::Vector3{0.0f, 0.0f, 0.0f};
        return NS::Obj::OverlapBoxCollision(collider, region);
    }

    [[nodiscard]] bool AABBContainsPoint(const NS::AABB& box, const NS::Vector3& p) noexcept
    {
        return p.x >= box.Center.x - box.Extents.x && p.x <= box.Center.x + box.Extents.x &&
               p.y >= box.Center.y - box.Extents.y && p.y <= box.Center.y + box.Extents.y &&
               p.z >= box.Center.z - box.Extents.z && p.z <= box.Center.z + box.Extents.z;
    }

    [[nodiscard]] bool IsBlockedAt(const NS::Obj::IUseCollision& collider, const NS::Vector3& point)
    {
        for (const NS::AABB& box : BoxesAtPoint(collider, point))
        {
            if (AABBContainsPoint(box, point))
            {
                return true;
            }
        }
        return false;
    }
} // namespace

bool Player::LedgeGrab() noexcept
{
    NS::Obj::Body& body = *m_body;
    if (!GL::Player::PlayerJudgeLedgeGrab::Judge(body.IsGrounded(), body.VerticalVelocity(), m_slam.wasSlamming))
    {
        return false;
    }

    NS::Vector3 dir{};
    if (!NS::TryNormalizeHorizontal(m_facingDir, dir))
    {
        return false;
    }

    // 手の高さ = カプセルの円柱部の上端。そこから前方へ伸ばした probe 点がブロックの XZ 内に入り、
    // かつブロック上端が手の上下の帯に収まれば縁とみなす
    // 縁は立ち姿で掴む。当たりの足元に立ち姿を立てた中心と、立ち姿の半長で測る。玉の間は根が半長ぶん下がっている
    // 玉の寸法のまま測ると手が円柱の長さぶん低い所を探し、縁の横を玉で落ちている間は掴めなかった
    const NS::Obj::Collider& collider = *m_collider;
    const float halfHeight = collider.StandingHalfHeight();
    NS::Vector3 pos = Root().Position();
    pos.y += halfHeight - collider.CapsuleHalfHeight();
    const float handY = pos.y + halfHeight;
    const NS::Vector3 probe{
        pos.x + dir.x * (collider.CapsuleRadius() + m_params->m_ledgeReach),
        handY,
        pos.z + dir.z * (collider.CapsuleRadius() + m_params->m_ledgeReach),
    };

    // 帯の上は今フレーム動いた距離まで。速く落ちると 1 フレームで縁の上端を通り過ぎて掴み損ねる
    const float above = m_lastMoveDistance;
    for (const NS::AABB& box : BoxesTouchingBand(collider, probe, m_params->m_ledgeGrabBelowHand, above))
    {
        const float top = box.Center.y + box.Extents.y;
        if (!GL::Player::PlayerJudgeLedgeGrab::InBand(probe, box, m_params->m_ledgeGrabBelowHand, above))
        {
            continue;
        }

        // 接近軸の優勢成分で掴む手前面を決め、その外側にカプセルを寄せた hang 位置を出す
        NS::Vector3 faceNormal{0.0f, 0.0f, 0.0f};
        NS::Vector3 hang = pos;
        if (std::abs(dir.x) >= std::abs(dir.z))
        {
            float sgn = -1.0f;
            if (dir.x >= 0.0f)
            {
                sgn = 1.0f;
            }
            const float faceX = box.Center.x - sgn * box.Extents.x;
            faceNormal = NS::Vector3{-sgn, 0.0f, 0.0f};
            hang.x = faceX - sgn * collider.CapsuleRadius();
            hang.z = NS::Clamp(pos.z, box.Center.z - box.Extents.z, box.Center.z + box.Extents.z);
        }
        else
        {
            float sgn = -1.0f;
            if (dir.z >= 0.0f)
            {
                sgn = 1.0f;
            }
            const float faceZ = box.Center.z - sgn * box.Extents.z;
            faceNormal = NS::Vector3{0.0f, 0.0f, -sgn};
            hang.z = faceZ - sgn * collider.CapsuleRadius();
            hang.x = NS::Clamp(pos.x, box.Center.x - box.Extents.x, box.Center.x + box.Extents.x);
        }
        hang.y = top - halfHeight;

        // 上面手前の登り先が別ブロックで塞がっているなら縁ではない。掴まない
        const float mantleStep = 2.0f * collider.CapsuleRadius();
        const NS::Vector3 mantleCheck{
            hang.x - faceNormal.x * mantleStep,
            top + halfHeight,
            hang.z - faceNormal.z * mantleStep,
        };
        if (IsBlockedAt(collider, mantleCheck))
        {
            continue;
        }

        // 掴まりからは突進が出ないので、立ち姿でぶら下がる。ぶら下がる位置は立ち姿の中心なので、
        // 置く前に解く。置いた後に解くと根が半長ぶん上がる
        ChangeCurled(false);
        Root().SetPosition(hang);
        body.SetVelocity(NS::Vector3{0.0f, 0.0f, 0.0f});
        m_ledgeTopY = top;
        m_ledgeFaceNormal = faceNormal;
        (void)m_states->Change<GL::Player::LedgeHangingPlayerState>();
        m_playerEvents.onLedgeGrabbed.Invoke();
        return true;
    }
    return false;
}

bool Player::HoldLedge() noexcept
{
    NS::Obj::Body& body = *m_body;
    float top = 0.0f;
    if (!FindLedgeTopAt(Root().Position(), top))
    {
        DropLedge();
        return false;
    }

    m_ledgeTopY = top;
    NS::Vector3 pos = Root().Position();
    pos.y = m_ledgeTopY - m_collider->CapsuleHalfHeight();
    Root().SetPosition(pos);
    body.SetVelocity(NS::Vector3{0.0f, 0.0f, 0.0f});
    return true;
}

bool Player::LedgeJump() noexcept
{
    if (!m_input->JumpPressed())
    {
        return false;
    }

    NS::Obj::Body& body = *m_body;
    body.SetVerticalVelocity(m_params->m_jumpImpulse);
    body.SetGrounded(false);
    (void)m_states->Change<GL::Player::FallPlayerState>();
    m_playerEvents.onJump.Invoke();
    return true;
}

void Player::ClimbLedge() noexcept
{
    NS::Obj::Body& body = *m_body;
    const NS::Obj::Collider& collider = *m_collider;
    const NS::Vector3 pos = Root().Position();
    // ぶら下がりの中心は面から半径ぶん外。直径ぶん奥へ進めると中心が縁から半径ぶん内側に入り、体が上面に乗る
    const float mantleStep = 2.0f * collider.CapsuleRadius();
    m_ledgeMantleStart = pos;
    m_ledgeMantleEnd = NS::Vector3{
        pos.x - m_ledgeFaceNormal.x * mantleStep,
        m_ledgeTopY + collider.CapsuleHalfHeight() + collider.CapsuleRadius(),
        pos.z - m_ledgeFaceNormal.z * mantleStep,
    };
    m_ledgeMantleTimer = 0.0f;
    (void)m_states->Change<GL::Player::LedgeClimbingPlayerState>();
    body.SetVelocity(NS::Vector3{0.0f, 0.0f, 0.0f});
    m_playerEvents.onLedgeClimbing.Invoke();
}

void Player::DropLedge() noexcept
{
    NS::Obj::Body& body = *m_body;
    (void)m_states->Change<GL::Player::FallPlayerState>();
    body.SetVelocity(NS::Vector3{0.0f, 0.0f, 0.0f});
    body.SetGrounded(false);
    // 壁と逆を向いて落ちる。壁を向いたままだと、帯の上の余白に縁が入って次のフレームで掴み直す
    m_facingDir = m_ledgeFaceNormal;
}

void Player::Shimmy(float dt) noexcept
{
    if (ClimbRight() != 0.0f)
    {
        // 面法線に水平直交する縁方向。動いても面からの距離は変わらない
        const NS::Vector3 pos = Root().Position();
        const NS::Vector3 alongDir{-m_ledgeFaceNormal.z, 0.0f, m_ledgeFaceNormal.x};
        NS::Vector3 shimmied = pos;
        shimmied.x += alongDir.x * ClimbRight() * m_params->m_ledgeShimmySpeed * dt;
        shimmied.z += alongDir.z * ClimbRight() * m_params->m_ledgeShimmySpeed * dt;
        // 移動先にも掴める縁が続いている時だけ動く。端なら止めて落とさない
        float top = 0.0f;
        if (FindLedgeTopAt(shimmied, top))
        {
            Root().SetPosition(shimmied);
        }
    }
}

void Player::UpdateLedgeClimb(float dt) noexcept
{
    NS::Obj::Body& body = *m_body;
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
        m_jumpsRemaining = 1;
        m_coyoteTimer = m_params->m_coyoteTime;
    }
}

bool Player::FindLedgeTopAt(const NS::Vector3& hangPos, float& outTop) const noexcept
{
    const NS::Obj::Collider& collider = *m_collider;
    const NS::Vector3 inward{-m_ledgeFaceNormal.x, 0.0f, -m_ledgeFaceNormal.z};
    const float handY = hangPos.y + collider.CapsuleHalfHeight();
    const NS::Vector3 probe{
        hangPos.x + inward.x * (collider.CapsuleRadius() + m_params->m_ledgeReach),
        handY,
        hangPos.z + inward.z * (collider.CapsuleRadius() + m_params->m_ledgeReach),
    };

    for (const NS::AABB& box : BoxesTouchingBand(collider, probe, m_params->m_ledgeGrabBelowHand, 0.0f))
    {
        if (!GL::Player::PlayerJudgeLedgeGrab::InBand(probe, box, m_params->m_ledgeGrabBelowHand, 0.0f))
        {
            continue;
        }
        const float top = box.Center.y + box.Extents.y;

        // 乗り上がり先が別ブロックで塞がっていたら縁とみなさない。オーバーハングの下では掴めない
        const float mantleStep = 2.0f * collider.CapsuleRadius();
        const NS::Vector3 mantleCheck{
            hangPos.x - m_ledgeFaceNormal.x * mantleStep,
            top + collider.CapsuleHalfHeight(),
            hangPos.z - m_ledgeFaceNormal.z * mantleStep,
        };
        if (IsBlockedAt(collider, mantleCheck))
        {
            continue;
        }

        outTop = top;
        return true;
    }
    return false;
}
