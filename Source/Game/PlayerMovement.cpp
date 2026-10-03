#include "Game/Player.h"

#include "Game/Player/HorizontalTurn.h"
#include "Game/Player/PlayerGravity.h"
#include "Game/Player/PlayerJudges.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/LedgeClimbingPlayerState.h"
#include "Game/Player/States/LedgeHangingPlayerState.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/Scene/Scene.h"

#include <algorithm>
#include <cmath>

namespace
{
    // from を Y 軸まわりに最大 maxRadians だけ to へ寄せた向き。from と to は正規化した水平の向き。
    // 使うのは MoveBody の振り向きだけなので、ここに置く
    NS::Core::Vector3 TurnHorizontalToward(const NS::Core::Vector3& from,
                                           const NS::Core::Vector3& to,
                                           float maxRadians) noexcept
    {
        const float angle = NS::Game::Player::HorizontalAngleBetween(from, to);
        if (std::abs(angle) <= maxRadians)
        {
            return to;
        }
        float step = maxRadians;
        if (angle < 0.0f)
        {
            step = -maxRadians;
        }
        return NS::Game::Player::RotateHorizontal(from, step);
    }
} // namespace

// ---- 移動の組み立て ----
// 状態の OnStep と、身体の段 (BodyStep) から呼ぶ MoveBody が、ここの関数を呼ぶ。
// 身体 (Body) の速度・接地・重力の計算を呼ぶだけで、値は PlayerParams から読む

void Player::MoveBody(float dt) noexcept
{
    // 壁に当たった後の速度からは面へ向かう分が抜ける。掴む向きに使うので、抜ける前の向きを覚える
    NS::Core::Vector3 target{};
    if (NS::Core::TryNormalizeHorizontal(m_body->LateralVelocity(), target))
    {
        // 一定の速さで回す。速さの理由は m_turnSpeed の欄
        NS::Core::Vector3 current{};
        if (m_params->m_turnSpeed > 0.0f && NS::Core::TryNormalizeHorizontal(m_facingDir, current))
        {
            const float maxTurn = NS::Core::ToRadians(NS::Core::Degrees{m_params->m_turnSpeed * dt}).value;
            m_facingDir = TurnHorizontalToward(current, target, maxTurn);
        }
        else
        {
            m_facingDir = target;
        }
    }

    const NS::Core::Vector3 before = Root().Position();
    m_body->Move(dt, m_params->m_maxStepHeight);
    const NS::Core::Vector3 delta = Root().Position() - before;
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

void Player::AccelerateToInputDirection(float dt) noexcept
{
    NS::Obj::Body& body = *m_body;
    NS::Core::Vector3 direction{};
    if (!NS::Game::Player::PlayerJudgeMoveInput::Judge(DesiredSpeedScale(), m_params->StickDeadzone()) ||
        !NS::Core::TryNormalizeHorizontal(DesiredDirection(), direction))
    {
        return;
    }

    const float topSpeed = std::max(MaxSpeed() * DesiredSpeedScale(), m_params->m_walkSpeed);
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
    if (NS::Game::Player::PlayerJudgeJump::Judge(
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
    body.Gravity(NS::Game::Player::ChooseGravity(m_params->Gravity(), body.VerticalVelocity()), dt);
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
    if (!(airSeconds > NS::Core::k_Epsilon))
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
    body.Gravity(NS::Game::Player::ChooseGravity(m_params->ReboundGravity(), body.VerticalVelocity()), dt);
}

void Player::AccelerateDuringRebound(float dt) noexcept
{
    NS::Obj::Body& body = *m_body;
    NS::Core::Vector3 direction{};
    if (!NS::Game::Player::PlayerJudgeMoveInput::Judge(DesiredSpeedScale(), m_params->StickDeadzone()) ||
        !NS::Core::TryNormalizeHorizontal(DesiredDirection(), direction))
    {
        return;
    }

    const float topSpeed = std::max(MaxSpeed() * DesiredSpeedScale(), m_params->m_walkSpeed);
    // 入力の向きからずれた速度は削らない。削ると横へ倒しただけで相手から離れる流れが消え、
    // 弾かれる向きが当て方でなくスティックで決まる。触って詰める値ではないので欄にしない
    const float turningDrag = 0.0f;
    // 明けのフレームは止める前の接地の印が残っている。接地を見て地上の加速度を選ぶと、そのフレームだけ大きく曲がる
    body.Accelerate(direction, turningDrag, m_params->m_reboundAirAcceleration, topSpeed, dt);
}

void Player::UpdateBodySlam(float dt) noexcept
{
    NS::Obj::Body& body = *m_body;
    // 突進中に向きを変えられると当てる間合いを詰める意味が消えるので、水平は発動時の値で書き直す
    if (!m_slam.isTap)
    {
        body.SetLateralVelocity(NS::Core::Vector3{
            m_slam.dir.x * m_params->m_bodySlamSpeed, 0.0f, m_slam.dir.z * m_params->m_bodySlamSpeed});
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
    [[nodiscard]] std::vector<NS::Core::AABB> BoxesTouchingBand(const NS::Obj::IUseCollision& body,
                                                                const NS::Core::Vector3& probe,
                                                                float below,
                                                                float above)
    {
        NS::Core::AABB region;
        region.Center = NS::Core::Vector3{probe.x, probe.y + 0.5f * (above - below), probe.z};
        region.Extents = NS::Core::Vector3{0.0f, 0.5f * (above + below), 0.0f};
        return NS::Obj::OverlapBoxCollision(body, region);
    }

    [[nodiscard]] std::vector<NS::Core::AABB> BoxesAtPoint(const NS::Obj::IUseCollision& body,
                                                           const NS::Core::Vector3& point)
    {
        NS::Core::AABB region;
        region.Center = point;
        region.Extents = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        return NS::Obj::OverlapBoxCollision(body, region);
    }

    [[nodiscard]] bool AABBContainsPoint(const NS::Core::AABB& box, const NS::Core::Vector3& p) noexcept
    {
        return p.x >= box.Center.x - box.Extents.x && p.x <= box.Center.x + box.Extents.x &&
               p.y >= box.Center.y - box.Extents.y && p.y <= box.Center.y + box.Extents.y &&
               p.z >= box.Center.z - box.Extents.z && p.z <= box.Center.z + box.Extents.z;
    }
} // namespace

bool Player::LedgeGrab() noexcept
{
    NS::Obj::Body& body = *m_body;
    if (!NS::Game::Player::PlayerJudgeLedgeGrab::Judge(body.IsGrounded(), body.VerticalVelocity(), m_slam.wasSlamming))
    {
        return false;
    }

    NS::Core::Vector3 dir{};
    if (!NS::Core::TryNormalizeHorizontal(m_facingDir, dir))
    {
        return false;
    }

    // 手の高さ = カプセルの円柱部の上端。そこから前方へ伸ばした probe 点がブロックの XZ 内に入り、
    // かつブロック上端が手の上下の帯に収まれば縁とみなす
    // 縁は立ち姿で掴む。当たりの足元に立ち姿を立てた中心と、立ち姿の半長で測る。玉の間は根が半長ぶん下がっている
    // 玉の寸法のまま測ると手が円柱の長さぶん低い所を探し、縁の横を玉で落ちている間は掴めなかった
    const float halfHeight = body.StandingHalfHeight();
    NS::Core::Vector3 pos = Root().Position();
    pos.y += halfHeight - body.CapsuleHalfHeight();
    const float handY = pos.y + halfHeight;
    const NS::Core::Vector3 probe{
        pos.x + dir.x * (body.CapsuleRadius() + m_params->m_ledgeReach),
        handY,
        pos.z + dir.z * (body.CapsuleRadius() + m_params->m_ledgeReach),
    };

    // 帯の上は今フレーム動いた距離まで。速く落ちると 1 フレームで縁の上端を通り過ぎて掴み損ねる
    const float above = m_lastMoveDistance;
    for (const NS::Core::AABB& box :
         BoxesTouchingBand(body, probe, m_params->m_ledgeGrabBelowHand, above))
    {
        const float top = box.Center.y + box.Extents.y;
        if (!NS::Game::Player::PlayerJudgeLedgeGrab::InBand(probe, box, m_params->m_ledgeGrabBelowHand, above))
        {
            continue;
        }

        // 接近軸の優勢成分で掴む手前面を決め、その外側にカプセルを寄せた hang 位置を出す
        NS::Core::Vector3 faceNormal{0.0f, 0.0f, 0.0f};
        NS::Core::Vector3 hang = pos;
        if (std::abs(dir.x) >= std::abs(dir.z))
        {
            float sgn = -1.0f;
            if (dir.x >= 0.0f)
            {
                sgn = 1.0f;
            }
            const float faceX = box.Center.x - sgn * box.Extents.x;
            faceNormal = NS::Core::Vector3{-sgn, 0.0f, 0.0f};
            hang.x = faceX - sgn * body.CapsuleRadius();
            hang.z = NS::Core::Clamp(pos.z, box.Center.z - box.Extents.z, box.Center.z + box.Extents.z);
        }
        else
        {
            float sgn = -1.0f;
            if (dir.z >= 0.0f)
            {
                sgn = 1.0f;
            }
            const float faceZ = box.Center.z - sgn * box.Extents.z;
            faceNormal = NS::Core::Vector3{0.0f, 0.0f, -sgn};
            hang.z = faceZ - sgn * body.CapsuleRadius();
            hang.x = NS::Core::Clamp(pos.x, box.Center.x - box.Extents.x, box.Center.x + box.Extents.x);
        }
        hang.y = top - halfHeight;

        // 上面手前の登り先が別ブロックで塞がっているなら縁ではない。掴まない
        const float mantleStep = 2.0f * body.CapsuleRadius();
        const NS::Core::Vector3 mantleCheck{
            hang.x - faceNormal.x * mantleStep,
            top + halfHeight,
            hang.z - faceNormal.z * mantleStep,
        };
        bool blocked = false;
        for (const NS::Core::AABB& other : BoxesAtPoint(body, mantleCheck))
        {
            if (AABBContainsPoint(other, mantleCheck))
            {
                blocked = true;
                break;
            }
        }
        if (blocked)
        {
            continue;
        }

        // 掴まりからは突進が出ないので、立ち姿でぶら下がる。ぶら下がる位置は立ち姿の中心なので、
        // 置く前に解く。置いた後に解くと根が半長ぶん上がる
        ChangeCurled(false);
        Root().SetPosition(hang);
        body.SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
        m_ledgeTopY = top;
        m_ledgeFaceNormal = faceNormal;
        (void)m_states->Change<NS::Game::Player::LedgeHangingPlayerState>();
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
    NS::Core::Vector3 pos = Root().Position();
    pos.y = m_ledgeTopY - body.CapsuleHalfHeight();
    Root().SetPosition(pos);
    body.SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
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
    (void)m_states->Change<NS::Game::Player::FallPlayerState>();
    m_playerEvents.onJump.Invoke();
    return true;
}

void Player::ClimbLedge() noexcept
{
    NS::Obj::Body& body = *m_body;
    const NS::Core::Vector3 pos = Root().Position();
    // ぶら下がりの中心は面から半径ぶん外。直径ぶん奥へ進めると中心が縁から半径ぶん内側に入り、体が上面に乗る
    const float mantleStep = 2.0f * body.CapsuleRadius();
    m_ledgeMantleStart = pos;
    m_ledgeMantleEnd = NS::Core::Vector3{
        pos.x - m_ledgeFaceNormal.x * mantleStep,
        m_ledgeTopY + body.CapsuleHalfHeight() + body.CapsuleRadius(),
        pos.z - m_ledgeFaceNormal.z * mantleStep,
    };
    m_ledgeMantleTimer = 0.0f;
    (void)m_states->Change<NS::Game::Player::LedgeClimbingPlayerState>();
    body.SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
    m_playerEvents.onLedgeClimbing.Invoke();
}

void Player::DropLedge() noexcept
{
    NS::Obj::Body& body = *m_body;
    (void)m_states->Change<NS::Game::Player::FallPlayerState>();
    body.SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
    body.SetGrounded(false);
    // 壁と逆を向いて落ちる。壁を向いたままだと、帯の上の余白に縁が入って次のフレームで掴み直す
    m_facingDir = m_ledgeFaceNormal;
}

void Player::Shimmy(float dt) noexcept
{
    if (ClimbRight() != 0.0f)
    {
        // 面法線に水平直交する縁方向。動いても面からの距離は変わらない
        const NS::Core::Vector3 pos = Root().Position();
        const NS::Core::Vector3 alongDir{-m_ledgeFaceNormal.z, 0.0f, m_ledgeFaceNormal.x};
        NS::Core::Vector3 shimmied = pos;
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
        t = NS::Core::Clamp(m_ledgeMantleTimer / m_params->m_ledgeClimbDuration, 0.0f, 1.0f);
    }

    // 2 段に割るのは角への食い込みを避けるため。前半は上昇だけで前へ進まない
    NS::Core::Vector3 pos{0.0f, 0.0f, 0.0f};
    if (t < 0.5f)
    {
        const float u = t / 0.5f;
        pos.x = m_ledgeMantleStart.x;
        pos.z = m_ledgeMantleStart.z;
        pos.y = m_ledgeMantleStart.y + (m_ledgeMantleEnd.y - m_ledgeMantleStart.y) * u;
    }
    else
    {
        const float u = (t - 0.5f) / 0.5f;
        pos.x = m_ledgeMantleStart.x + (m_ledgeMantleEnd.x - m_ledgeMantleStart.x) * u;
        pos.z = m_ledgeMantleStart.z + (m_ledgeMantleEnd.z - m_ledgeMantleStart.z) * u;
        pos.y = m_ledgeMantleEnd.y;
    }
    Root().SetPosition(pos);
    body.SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});

    if (t >= 1.0f)
    {
        Root().SetPosition(m_ledgeMantleEnd);
        (void)m_states->Change<NS::Game::Player::IdlePlayerState>();
        body.SetGrounded(true);
        m_jumpsRemaining = 1;
        m_coyoteTimer = m_params->m_coyoteTime;
    }
}

bool Player::FindLedgeTopAt(const NS::Core::Vector3& hangPos, float& outTop) const noexcept
{
    const NS::Obj::Body& body = *m_body;
    const NS::Core::Vector3 inward{-m_ledgeFaceNormal.x, 0.0f, -m_ledgeFaceNormal.z};
    const float handY = hangPos.y + body.CapsuleHalfHeight();
    const NS::Core::Vector3 probe{
        hangPos.x + inward.x * (body.CapsuleRadius() + m_params->m_ledgeReach),
        handY,
        hangPos.z + inward.z * (body.CapsuleRadius() + m_params->m_ledgeReach),
    };

    for (const NS::Core::AABB& box :
         BoxesTouchingBand(body, probe, m_params->m_ledgeGrabBelowHand, 0.0f))
    {
        const float top = box.Center.y + box.Extents.y;
        if (top < handY - m_params->m_ledgeGrabBelowHand || top > handY)
        {
            continue;
        }
        if (probe.x < box.Center.x - box.Extents.x || probe.x > box.Center.x + box.Extents.x)
        {
            continue;
        }
        if (probe.z < box.Center.z - box.Extents.z || probe.z > box.Center.z + box.Extents.z)
        {
            continue;
        }

        // 乗り上がり先が別ブロックで塞がっていたら縁とみなさない。オーバーハングの下では掴めない
        const float mantleStep = 2.0f * body.CapsuleRadius();
        const NS::Core::Vector3 mantleCheck{
            hangPos.x - m_ledgeFaceNormal.x * mantleStep,
            top + body.CapsuleHalfHeight(),
            hangPos.z - m_ledgeFaceNormal.z * mantleStep,
        };
        bool blocked = false;
        for (const NS::Core::AABB& other : BoxesAtPoint(body, mantleCheck))
        {
            if (AABBContainsPoint(other, mantleCheck))
            {
                blocked = true;
                break;
            }
        }
        if (blocked)
        {
            continue;
        }

        outTop = top;
        return true;
    }
    return false;
}
