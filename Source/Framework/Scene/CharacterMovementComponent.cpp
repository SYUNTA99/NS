#include "Framework/Scene/CharacterMovementComponent.h"

#include "Framework/Core/Clock.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Graphics/DebugDraw.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/PoleComponent.h"
#include "Framework/Scene/Transform.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kHorizontalSpeedEpsilon = 0.01f;

    /// 一次遅れの離散化。tau = 時定数 (大きいほど鈍い)、dt = step。0 < tau で安定
    [[nodiscard]] float SmoothApproach(float current, float target, float tau, float dt) noexcept
    {
        if (tau <= 0.0f)
            return target;
        const float a = 1.0f - std::exp(-dt / tau);
        return current + (target - current) * a;
    }

    [[nodiscard]] NS::Math::Vector3 HorizontalSmooth(const NS::Math::Vector3& curr,
                                                     const NS::Math::Vector3& target,
                                                     float tau,
                                                     float dt) noexcept
    {
        return NS::Math::Vector3{
            SmoothApproach(curr.x, target.x, tau, dt),
            curr.y,
            SmoothApproach(curr.z, target.z, tau, dt),
        };
    }

    /// pole 掴まり中の上下移動速度 (m/s)。 入力 1.0 で kClimbSpeed のレート
    constexpr float kClimbSpeed = 2.0f;
    /// 離脱 jump 時、 接触面の逆方向に与える初速 (m/s)
    constexpr float kClimbExitOutwardSpeed = 3.0f;
    /// 離脱 jump 時、 上方向に与える初速 (m/s)
    constexpr float kClimbExitUpwardSpeed = 6.0f;
    /// auto-mantle 判定の上端余裕 (m)。 pole top にこの距離まで近づいたら歩行へ
    constexpr float kClimbMantleEpsilon = 0.05f;

    /// 縁掴み: 手 (capsule 上端) と block 上端の高さ差の許容下幅 / 上幅 (m)。 この帯に
    /// block 上端が入ると掴める。 GUI playtest で詰める初期値
    constexpr float kLedgeGrabBandLow = 0.5f;
    constexpr float kLedgeGrabBandHigh = 0.5f;
    /// 縁掴み: capsule 表面から前方へ手を伸ばす追加距離 (m)
    constexpr float kLedgeReach = 0.3f;
    /// 縁掴み: mantle 時に面の内側へ押し込む余白 (m)。 2*radius に上乗せして上面へ確実に乗せる
    constexpr float kLedgeMantleInset = 0.1f;
    /// 縁掴み: mantle 後に block 上面から浮かせる安全マージン (m)。 spawn lift と同趣旨
    constexpr float kLedgeMantleLift = 0.02f;
    /// 縁掴み: mantle / drop を起動する climb 前後入力のしきい値
    constexpr float kLedgeInputThreshold = 0.5f;
    /// 縁掴み: つかんだ後、 前入力での自動登りを許すまでの最小ぶら下がり時間 (s)。 壁に向かう
    /// 入力のまま即登り切ってつかみが見えない問題を防ぐ。 jump / drop はこの待ちを受けない
    constexpr float kLedgeMinHangTime = 0.3f;
    /// 縁掴み: ぶら下がりから上面へよじ登る mantle モーションの所要時間 (s)。 瞬間移動を避けて
    /// 登りを視認できるようにする。 前半で上昇、 後半で前進の 2 段に割る
    constexpr float kLedgeMantleDuration = 0.25f;
    /// 縁掴み: シミー (縁沿い左右移動) の速度 (m/s) と入力デッドゾーン
    constexpr float kLedgeShimmySpeed = 2.0f;
    constexpr float kLedgeShimmyDeadzone = 0.3f;
    /// 縁掴み: シミー継続判定で「同じ高さの縁」とみなす上端の許容差 (m)
    constexpr float kLedgeContinueTopTol = 0.1f;
    /// 縁掴み: drop 時に面法線方向へ離す距離 (m) と初速 (m/s)
    constexpr float kLedgeDropOutward = 0.2f;
    constexpr float kLedgeDropOutwardSpeed = 2.0f;
    /// 縁掴み: drop / mantle 直後に再掴みを禁止する時間 (s)。 放しても入力を倒し続けた時の
    /// 即再掴みを防ぐ
    constexpr float kLedgeRegrabCooldownTime = 0.3f;

    [[nodiscard]] bool AabbContainsPoint(const NS::Math::AABB& box, const NS::Math::Vector3& p) noexcept
    {
        return p.x >= box.Center.x - box.Extents.x && p.x <= box.Center.x + box.Extents.x &&
               p.y >= box.Center.y - box.Extents.y && p.y <= box.Center.y + box.Extents.y &&
               p.z >= box.Center.z - box.Extents.z && p.z <= box.Center.z + box.Extents.z;
    }
} // namespace

namespace NS::Scene
{
    CharacterMovementComponent::CharacterMovementComponent() noexcept {}

    void CharacterMovementComponent::SetDesiredMove(const NS::Math::Vector3& worldDir, float speedScale01) noexcept
    {
        m_desiredDir = worldDir;
        m_desiredSpeedScale = NS::Math::Clamp(speedScale01, 0.0f, 1.0f);
    }

    void CharacterMovementComponent::SetClimbMove(float localRight, float localForward) noexcept
    {
        m_climbRight = NS::Math::Clamp(localRight, -1.0f, 1.0f);
        m_climbForward = NS::Math::Clamp(localForward, -1.0f, 1.0f);
    }

    void CharacterMovementComponent::SetJumpPressed() noexcept
    {
        m_jumpPressedThisFrame = true;
    }

    void CharacterMovementComponent::SetJumpHeld(bool held) noexcept
    {
        m_jumpHeld = held;
    }

    void CharacterMovementComponent::SetCollisionWorld(std::span<const NS::Math::AABB> world)
    {
        m_collisionWorld.assign(world.begin(), world.end());
    }

    void CharacterMovementComponent::SetCollisionTriangles(std::span<const NS::Physics::Triangle> triangles)
    {
        m_collisionTriangles.assign(triangles.begin(), triangles.end());
    }

    void CharacterMovementComponent::SetClimbables(std::span<PoleComponent* const> poles) noexcept
    {
        m_poles = poles;
    }

    void CharacterMovementComponent::ResetState() noexcept
    {
        m_velocity = NS::Math::Vector3{0.0f, 0.0f, 0.0f};
        m_desiredDir = NS::Math::Vector3{0.0f, 0.0f, 0.0f};
        m_desiredSpeedScale = 0.0f;
        m_climbRight = 0.0f;
        m_climbForward = 0.0f;
        m_jumpHeld = false;
        m_prevJumpHeld = false;
        m_jumpPressedThisFrame = false;
        m_jumpsRemaining = 1;
        m_coyoteTimer = 0.0f;
        m_bufferTimer = 0.0f;
        m_wasGrounded = false;
        m_isGrounded = false;
        m_state = MovementState::Walking;
        m_attachedPole = nullptr;
        m_ledgeTopY = 0.0f;
        m_ledgeFaceNormal = NS::Math::Vector3{0.0f, 0.0f, 0.0f};
        m_ledgeRegrabCooldown = 0.0f;
        m_ledgeHangTimer = 0.0f;
        m_ledgeMantleTimer = 0.0f;
    }

    void CharacterMovementComponent::OnUpdate()
    {
        NS_SCOPED_TIMER(::NS::Core::LogCat::Game, "CharacterMovement::OnUpdate");

        const float dt = NS::Core::FrameTimer::FixedDelta();

        if (!IsActive() || dt <= 0.0f)
        {
            m_jumpPressedThisFrame = false;
            m_prevJumpHeld = m_jumpHeld;
            return;
        }

        // ClimbingPole では default CharacterController を bypass し、 pole の axis に拘束された
        // 専用 update で position を直接更新する (Mario-style non-physical controller)
        if (m_state == MovementState::ClimbingPole)
        {
            // 離脱 jump: pole から outward (XZ 半径方向) + 上方向に飛び離れて Falling へ
            if (m_jumpPressedThisFrame && m_attachedPole != nullptr)
            {
                const NS::Math::Vector3 pos = RootTransform().Position();
                const NS::Math::Vector3 axisStart = m_attachedPole->AxisStart();
                NS::Math::Vector3 outward{pos.x - axisStart.x, 0.0f, pos.z - axisStart.z};
                const float len = std::sqrt(outward.x * outward.x + outward.z * outward.z);
                if (len > 1e-4f)
                {
                    outward.x /= len;
                    outward.z /= len;
                }
                else
                {
                    outward = NS::Math::Vector3{1.0f, 0.0f, 0.0f};
                }
                m_velocity = NS::Math::Vector3{
                    outward.x * kClimbExitOutwardSpeed, kClimbExitUpwardSpeed, outward.z * kClimbExitOutwardSpeed};
                m_state = MovementState::Falling;
                m_attachedPole = nullptr;
                m_isGrounded = false;
                m_jumpPressedThisFrame = false;
                m_prevJumpHeld = m_jumpHeld;
                return;
            }

            if (m_attachedPole != nullptr)
            {
                NS::Math::Vector3 pos = RootTransform().Position();
                // 縦入力は climb 専用チャンネル (生ローカル前後入力) を使う。 camera 相対の
                // m_desiredDir を使うと camera 向き次第で上昇量が 0 になるため別系統で受ける
                // (前=上昇、 後=下降、 camera 非依存)
                const float verticalInput = m_climbForward;
                pos.y += verticalInput * kClimbSpeed * dt;

                const NS::Math::Vector3 axisStart = m_attachedPole->AxisStart();
                const NS::Math::Vector3 axisEnd = m_attachedPole->AxisEnd();
                if (pos.y < axisStart.y)
                    pos.y = axisStart.y;

                // 上端に達したら自動で mantle (Walking) へ遷移
                if (pos.y >= axisEnd.y - kClimbMantleEpsilon)
                {
                    pos.y = axisEnd.y;
                    RootTransform().SetPosition(pos);
                    m_state = MovementState::Walking;
                    m_attachedPole = nullptr;
                    m_velocity = NS::Math::Vector3{0.0f, 0.0f, 0.0f};
                    m_isGrounded = true;
                    m_jumpsRemaining = 1;
                    m_jumpPressedThisFrame = false;
                    m_prevJumpHeld = m_jumpHeld;
                    return;
                }

                // XZ は pole 軸に snap して安定させる
                pos.x = axisStart.x;
                pos.z = axisStart.z;
                RootTransform().SetPosition(pos);
                // velocity は climb logic が完全に支配する (gravity は無効、 controller も bypass)
                m_velocity = NS::Math::Vector3{0.0f, verticalInput * kClimbSpeed, 0.0f};
            }

            m_skipControllerLastFrame = true;
            m_jumpPressedThisFrame = false;
            m_prevJumpHeld = m_jumpHeld;
            return;
        }

        // LedgeHanging も controller を bypass し、 縁にぶら下がった専用更新で position を直接動かす
        if (m_state == MovementState::LedgeHanging)
        {
            UpdateLedgeHang(dt);
            m_skipControllerLastFrame = true;
            m_jumpPressedThisFrame = false;
            m_prevJumpHeld = m_jumpHeld;
            return;
        }

        if (m_state == MovementState::LedgeMantling)
        {
            UpdateLedgeMantle(dt);
            m_skipControllerLastFrame = true;
            m_jumpPressedThisFrame = false;
            m_prevJumpHeld = m_jumpHeld;
            return;
        }

        // 通常 (Walking / Jumping / Falling): 既存の物理ロジック
        if (m_ledgeRegrabCooldown > 0.0f)
            m_ledgeRegrabCooldown -= dt;

        m_bufferTimer -= dt;
        if (m_jumpPressedThisFrame)
            m_bufferTimer = m_jumpBufferTime;

        const bool inAir = !m_isGrounded;
        if (inAir)
            m_coyoteTimer -= dt;

        float targetSpeed = 0.0f;
        if (m_desiredSpeedScale >= m_stickDeadzone)
        {
            targetSpeed = (m_desiredSpeedScale < 0.5f) ? m_walkSpeed : (m_maxSpeed * m_desiredSpeedScale);
        }

        NS::Math::Vector3 targetHoriz{m_desiredDir.x * targetSpeed, 0.0f, m_desiredDir.z * targetSpeed};

        const float currHorizMag = std::sqrt(m_velocity.x * m_velocity.x + m_velocity.z * m_velocity.z);
        const float tau = (targetSpeed > currHorizMag + kHorizontalSpeedEpsilon) ? m_accelTau : m_decelTau;
        m_velocity = HorizontalSmooth(m_velocity, targetHoriz, tau, dt);

        const bool canGroundJump = (m_isGrounded || m_coyoteTimer > 0.0f) && m_jumpsRemaining > 0;
        const bool wantJump = m_jumpPressedThisFrame || m_bufferTimer > 0.0f;
        if (canGroundJump && wantJump)
        {
            m_velocity.y = m_jumpImpulse;
            --m_jumpsRemaining;
            m_bufferTimer = 0.0f;
            m_coyoteTimer = 0.0f;
        }
        else if (m_jumpPressedThisFrame && !m_isGrounded && m_coyoteTimer <= 0.0f && m_jumpsRemaining > 0)
        {
            m_velocity.y = m_jumpImpulse;
            --m_jumpsRemaining;
            m_bufferTimer = 0.0f;
        }

        if (m_prevJumpHeld && !m_jumpHeld && m_velocity.y > 0.0f)
            m_velocity.y *= m_jumpReleaseScale;

        const bool apex = std::abs(m_velocity.y) < m_apexHangVy;
        const float baseG = (m_velocity.y > 0.0f) ? m_gravityUp : m_gravityDown;
        const float g = apex ? baseG * m_apexHangScale : baseG;
        m_velocity.y += g * dt;

        NS::Physics::CharacterControllerInput in{};
        in.position = RootTransform().Position();
        in.velocity = m_velocity;
        in.dt = dt;
        in.capsuleRadius = m_capsuleRadius;
        in.capsuleHalfHeight = m_capsuleHalfHeight;
        in.world = std::span<const NS::Math::AABB>(m_collisionWorld);
        in.worldTriangles = std::span<const NS::Physics::Triangle>(m_collisionTriangles);
        const NS::Physics::CharacterControllerResult out = m_controller.Update(in);

        RootTransform().SetPosition(out.position);
        m_velocity = out.velocity;
        m_wasGrounded = m_isGrounded;
        m_isGrounded = out.grounded;

        if (!m_wasGrounded && m_isGrounded)
            m_jumpsRemaining = 1;

        if (m_isGrounded)
            m_coyoteTimer = m_coyoteTime;

        // Walking / Jumping / Falling のサブ分類は high-level state の参考にする (controller bypass はしない)
        if (m_isGrounded)
            m_state = MovementState::Walking;
        else if (m_velocity.y > 0.0f)
            m_state = MovementState::Jumping;
        else
            m_state = MovementState::Falling;

        // grab intent: 入力が pole に向いていて、 かつ player 中心が trigger 内なら掴まり状態へ
        if (m_desiredSpeedScale > m_stickDeadzone)
        {
            const NS::Math::Vector3 pos = out.position;
            for (PoleComponent* pole : m_poles)
            {
                if (pole != nullptr && pole->ContainsPoint(pos))
                {
                    m_attachedPole = pole;
                    m_state = MovementState::ClimbingPole;
                    m_velocity = NS::Math::Vector3{0.0f, 0.0f, 0.0f};
                    const NS::Math::Vector3 axisStart = pole->AxisStart();
                    RootTransform().SetPosition(NS::Math::Vector3{axisStart.x, pos.y, axisStart.z});
                    break;
                }
            }
        }

        // pole を掴んでいなければ、 通常 block の縁を掴めるか試す (空中下降中のみ成立)
        if (m_state != MovementState::ClimbingPole)
            TryGrabLedge(out.position);

        if (m_debugDraw)
        {
            const NS::Math::Vector3 center = RootTransform().Position();
            const NS::Math::Vector3 axis{0.0f, m_capsuleHalfHeight, 0.0f};
            const NS::Math::Color color =
                m_isGrounded ? NS::Math::Color{0.2f, 1.0f, 0.2f, 1.0f} : NS::Math::Color{1.0f, 1.0f, 0.2f, 1.0f};
            NS::Graphics::DebugDraw::Capsule(center, axis, m_capsuleRadius, color);
        }

        m_prevJumpHeld = m_jumpHeld;
        m_jumpPressedThisFrame = false;
        m_skipControllerLastFrame = false;
    }

    bool CharacterMovementComponent::TryGrabLedge(const NS::Math::Vector3& pos) noexcept
    {
        // 空中で下降中、 かつ前方入力がある時だけ掴む。 cooldown 中は無効
        if (m_ledgeRegrabCooldown > 0.0f || m_isGrounded || m_velocity.y > 0.0f)
            return false;
        if (m_desiredSpeedScale <= m_stickDeadzone)
            return false;

        NS::Math::Vector3 dir{m_desiredDir.x, 0.0f, m_desiredDir.z};
        const float dirLen = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        if (dirLen < 1e-4f)
            return false;
        dir.x /= dirLen;
        dir.z /= dirLen;

        // 手の高さ = capsule 上端。 そこから前方へ伸ばした probe 点が block の XZ 内に入り、
        // かつ block 上端が手の高さの帯に収まれば縁とみなす
        const float handY = pos.y + m_capsuleHalfHeight;
        const NS::Math::Vector3 probe{
            pos.x + dir.x * (m_capsuleRadius + kLedgeReach),
            handY,
            pos.z + dir.z * (m_capsuleRadius + kLedgeReach),
        };

        for (const NS::Math::AABB& box : m_collisionWorld)
        {
            const float top = box.Center.y + box.Extents.y;
            if (top < handY - kLedgeGrabBandLow || top > handY + kLedgeGrabBandHigh)
                continue;
            if (probe.x < box.Center.x - box.Extents.x || probe.x > box.Center.x + box.Extents.x)
                continue;
            if (probe.z < box.Center.z - box.Extents.z || probe.z > box.Center.z + box.Extents.z)
                continue;

            // 接近軸の優勢成分で掴む手前面を決め、 その外側に capsule を寄せた hang 位置を出す
            NS::Math::Vector3 faceNormal{0.0f, 0.0f, 0.0f};
            NS::Math::Vector3 hang = pos;
            if (std::abs(dir.x) >= std::abs(dir.z))
            {
                const float sgn = (dir.x >= 0.0f) ? 1.0f : -1.0f;
                const float faceX = box.Center.x - sgn * box.Extents.x;
                faceNormal = NS::Math::Vector3{-sgn, 0.0f, 0.0f};
                hang.x = faceX - sgn * m_capsuleRadius;
                hang.z = NS::Math::Clamp(pos.z, box.Center.z - box.Extents.z, box.Center.z + box.Extents.z);
            }
            else
            {
                const float sgn = (dir.z >= 0.0f) ? 1.0f : -1.0f;
                const float faceZ = box.Center.z - sgn * box.Extents.z;
                faceNormal = NS::Math::Vector3{0.0f, 0.0f, -sgn};
                hang.z = faceZ - sgn * m_capsuleRadius;
                hang.x = NS::Math::Clamp(pos.x, box.Center.x - box.Extents.x, box.Center.x + box.Extents.x);
            }
            hang.y = top - m_capsuleHalfHeight;

            // mantle 先 (上面の手前) が別 block で塞がっているなら縁ではない。 掴まない
            const float mantleStep = 2.0f * m_capsuleRadius + kLedgeMantleInset;
            const NS::Math::Vector3 mantleCheck{
                hang.x - faceNormal.x * mantleStep,
                top + m_capsuleHalfHeight,
                hang.z - faceNormal.z * mantleStep,
            };
            bool blocked = false;
            for (const NS::Math::AABB& other : m_collisionWorld)
            {
                if (AabbContainsPoint(other, mantleCheck))
                {
                    blocked = true;
                    break;
                }
            }
            if (blocked)
                continue;

            RootTransform().SetPosition(hang);
            m_velocity = NS::Math::Vector3{0.0f, 0.0f, 0.0f};
            m_ledgeTopY = top;
            m_ledgeFaceNormal = faceNormal;
            m_ledgeHangTimer = 0.0f;
            m_state = MovementState::LedgeHanging;
            return true;
        }
        return false;
    }

    void CharacterMovementComponent::UpdateLedgeHang(float dt) noexcept
    {
        m_ledgeHangTimer += dt;
        NS::Math::Vector3 pos = RootTransform().Position();

        // 登る: jump は即時、 前入力での自動登りは一瞬ぶら下がりを見せてから (最小ぶら下がり時間)
        // 面の内側へ押し込み block 上面に立たせて Walking へ
        const bool autoClimb = m_climbForward > kLedgeInputThreshold && m_ledgeHangTimer >= kLedgeMinHangTime;
        if (m_jumpPressedThisFrame || autoClimb)
        {
            const float mantleStep = 2.0f * m_capsuleRadius + kLedgeMantleInset;
            m_ledgeMantleStart = pos;
            m_ledgeMantleEnd = NS::Math::Vector3{
                pos.x - m_ledgeFaceNormal.x * mantleStep,
                m_ledgeTopY + m_capsuleHalfHeight + m_capsuleRadius + kLedgeMantleLift,
                pos.z - m_ledgeFaceNormal.z * mantleStep,
            };
            m_ledgeMantleTimer = 0.0f;
            m_state = MovementState::LedgeMantling;
            m_velocity = NS::Math::Vector3{0.0f, 0.0f, 0.0f};
            return;
        }

        // 落ちる: 後入力で手を放す。 面法線方向へ少し離して Falling、 即再掴みを cooldown で抑止
        if (m_climbForward < -kLedgeInputThreshold)
        {
            pos.x += m_ledgeFaceNormal.x * kLedgeDropOutward;
            pos.z += m_ledgeFaceNormal.z * kLedgeDropOutward;
            RootTransform().SetPosition(pos);
            m_state = MovementState::Falling;
            m_velocity = NS::Math::Vector3{
                m_ledgeFaceNormal.x * kLedgeDropOutwardSpeed, 0.0f, m_ledgeFaceNormal.z * kLedgeDropOutwardSpeed};
            m_isGrounded = false;
            m_ledgeRegrabCooldown = kLedgeRegrabCooldownTime;
            return;
        }

        // それ以外: 縁にぶら下がったまま、 左右入力で縁に沿ってシミー移動する (重力無効)
        pos.y = m_ledgeTopY - m_capsuleHalfHeight;
        if (std::abs(m_climbRight) > kLedgeShimmyDeadzone)
        {
            // 面法線に水平直交する縁方向。 移動しても面からの距離は変わらない
            const NS::Math::Vector3 alongDir{-m_ledgeFaceNormal.z, 0.0f, m_ledgeFaceNormal.x};
            NS::Math::Vector3 shimmied = pos;
            shimmied.x += alongDir.x * m_climbRight * kLedgeShimmySpeed * dt;
            shimmied.z += alongDir.z * m_climbRight * kLedgeShimmySpeed * dt;
            // 移動先にも同じ高さの縁が続いている時だけ動く。 端なら止めて落とさない
            if (LedgeContinuesAt(shimmied))
                pos = shimmied;
        }
        RootTransform().SetPosition(pos);
        m_velocity = NS::Math::Vector3{0.0f, 0.0f, 0.0f};
    }

    void CharacterMovementComponent::UpdateLedgeMantle(float dt) noexcept
    {
        m_ledgeMantleTimer += dt;
        const float t = NS::Math::Clamp(m_ledgeMantleTimer / kLedgeMantleDuration, 0.0f, 1.0f);

        // 前半で縁の高さまで上昇、 後半で上面へ前進する 2 段モーション。 角への食い込みを避ける
        NS::Math::Vector3 pos;
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
        RootTransform().SetPosition(pos);
        m_velocity = NS::Math::Vector3{0.0f, 0.0f, 0.0f};

        if (t >= 1.0f)
        {
            RootTransform().SetPosition(m_ledgeMantleEnd);
            m_state = MovementState::Walking;
            m_isGrounded = true;
            m_wasGrounded = true;
            m_jumpsRemaining = 1;
            m_coyoteTimer = m_coyoteTime;
            m_ledgeRegrabCooldown = kLedgeRegrabCooldownTime;
        }
    }

    bool CharacterMovementComponent::LedgeContinuesAt(const NS::Math::Vector3& hangPos) const noexcept
    {
        const NS::Math::Vector3 inward{-m_ledgeFaceNormal.x, 0.0f, -m_ledgeFaceNormal.z};
        const float handY = hangPos.y + m_capsuleHalfHeight;
        const NS::Math::Vector3 probe{
            hangPos.x + inward.x * (m_capsuleRadius + kLedgeReach),
            handY,
            hangPos.z + inward.z * (m_capsuleRadius + kLedgeReach),
        };

        for (const NS::Math::AABB& box : m_collisionWorld)
        {
            const float top = box.Center.y + box.Extents.y;
            if (std::abs(top - m_ledgeTopY) > kLedgeContinueTopTol)
                continue;
            if (probe.x < box.Center.x - box.Extents.x || probe.x > box.Center.x + box.Extents.x)
                continue;
            if (probe.z < box.Center.z - box.Extents.z || probe.z > box.Center.z + box.Extents.z)
                continue;

            // 乗り上がり先が別 block で塞がっていたら縁とみなさない (オーバーハングの下では掴めない)
            const float mantleStep = 2.0f * m_capsuleRadius + kLedgeMantleInset;
            const NS::Math::Vector3 mantleCheck{
                hangPos.x - m_ledgeFaceNormal.x * mantleStep,
                m_ledgeTopY + m_capsuleHalfHeight,
                hangPos.z - m_ledgeFaceNormal.z * mantleStep,
            };
            bool blocked = false;
            for (const NS::Math::AABB& other : m_collisionWorld)
            {
                if (AabbContainsPoint(other, mantleCheck))
                {
                    blocked = true;
                    break;
                }
            }
            if (blocked)
                continue;

            return true;
        }
        return false;
    }
} // namespace NS::Scene
