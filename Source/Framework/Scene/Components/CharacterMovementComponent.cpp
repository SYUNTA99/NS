#include "Framework/Scene/Components/CharacterMovementComponent.h"

#include "Framework/Core/Clock.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Physics/PhysicsWorld.h"
#include "Framework/Scene/ComponentRegistry.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/SceneBase.h"
#include "Framework/Scene/Transform.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace
{
    constexpr float kHorizontalSpeedEpsilon = 0.01f;

    /// ledge grab が走査する AABB 群を world から借りる。 world 未設定時は空で掴めない
    [[nodiscard]] std::span<const NS::Math::AABB> WorldAabbs(const NS::Physics::PhysicsWorld* world) noexcept
    {
        if (world == nullptr)
            return {};
        return std::span<const NS::Math::AABB>(world->Aabbs());
    }

    /// 一次遅れの離散化。tau は時定数で値が大きいほど鈍い、dt は step。0 < tau で安定
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

    /// 縁掴み: capsule 上端を手とみなし、 block 上端との高さ差の許容下幅 / 上幅で単位は m。 この帯に
    /// block 上端が入ると掴める。 GUI playtest で詰める初期値
    constexpr float kLedgeGrabBandLow = 0.5f;
    constexpr float kLedgeGrabBandHigh = 0.5f;
    /// 縁掴み: capsule 表面から前方へ手を伸ばす追加距離。 単位は m
    constexpr float kLedgeReach = 0.3f;
    /// 縁掴み: mantle 時に面の内側へ押し込む余白で単位は m。 2*radius に上乗せして上面へ確実に乗せる
    constexpr float kLedgeMantleInset = 0.1f;
    /// 縁掴み: mantle 後に block 上面から浮かせる安全マージンで単位は m。 spawn lift と同趣旨
    constexpr float kLedgeMantleLift = 0.02f;
    /// 縁掴み: mantle / drop を起動する climb 前後入力のしきい値
    constexpr float kLedgeInputThreshold = 0.5f;
    /// 縁掴み: つかんだ後、 前入力での自動登りを許すまでの最小ぶら下がり時間で単位は s。 壁に向かう
    /// 入力のまま即登り切ってつかみが見えない問題を防ぐ。 jump / drop はこの待ちを受けない
    constexpr float kLedgeMinHangTime = 0.3f;

    /// コヨーテジャンプ記録の表示寿命で単位は s。 直近の数試行を見比べられる長さ
    constexpr float kCoyoteJumpMarkerLifetime = 3.0f;
    /// 同時に保持するコヨーテジャンプ記録の上限。 画面が赤線で埋まらない数
    constexpr std::size_t kMaxCoyoteJumpMarkers = 16;
    /// 縁掴み: ぶら下がりから上面へよじ登る mantle モーションの所要時間で単位は s。 瞬間移動を避けて
    /// 登りを視認できるようにする。 前半で上昇、 後半で前進の 2 段に割る
    constexpr float kLedgeMantleDuration = 0.25f;
    /// 縁掴み: 縁に沿った左右移動シミーの速度と入力デッドゾーン、 速度の単位は m/s
    constexpr float kLedgeShimmySpeed = 2.0f;
    constexpr float kLedgeShimmyDeadzone = 0.3f;
    /// 縁掴み: シミー継続判定で「同じ高さの縁」とみなす上端の許容差。 単位は m
    constexpr float kLedgeContinueTopTol = 0.1f;
    /// 縁掴み: drop 時に面法線方向へ離す距離と初速で、 単位はそれぞれ m と m/s
    constexpr float kLedgeDropOutward = 0.2f;
    constexpr float kLedgeDropOutwardSpeed = 2.0f;
    /// 縁掴み: drop / mantle 直後に再掴みを禁止する時間で単位は s。 放しても入力を倒し続けた時の
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
        m_ledgeTopY = 0.0f;
        m_ledgeFaceNormal = NS::Math::Vector3{0.0f, 0.0f, 0.0f};
        m_ledgeRegrabCooldown = 0.0f;
        m_ledgeHangTimer = 0.0f;
        m_ledgeMantleTimer = 0.0f;
        m_lastGroundedPosition = NS::Math::Vector3{0.0f, 0.0f, 0.0f};
        m_coyoteJumpMarkers.clear();
    }

    void CharacterMovementComponent::OnStart()
    {
        if (m_world == nullptr && Owner() != nullptr && Owner()->OwningScene() != nullptr)
            m_world = &Owner()->OwningScene()->Physics();
    }

    void CharacterMovementComponent::PushCoyoteJumpMarker(const NS::Math::Vector3& edge,
                                                          const NS::Math::Vector3& jump) noexcept
    {
        if (m_coyoteJumpMarkers.size() >= kMaxCoyoteJumpMarkers)
            m_coyoteJumpMarkers.erase(m_coyoteJumpMarkers.begin());
        m_coyoteJumpMarkers.push_back(CoyoteJumpMarker{edge, jump, kCoyoteJumpMarkerLifetime});
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

#if !defined(NS_SHIPPING)
        // コヨーテジャンプ記録を寿命で減衰させる。 ledge で早期 return する状態でも確実に老化させるため
        // どの state へ分岐するより前に処理する
        for (CoyoteJumpMarker& marker : m_coyoteJumpMarkers)
            marker.remaining -= dt;
        std::erase_if(m_coyoteJumpMarkers, [](const CoyoteJumpMarker& m) { return m.remaining <= 0.0f; });
#endif

        // LedgeHanging も controller を bypass し、 縁にぶら下がった専用更新で position を直接動かす
        if (m_state == MovementState::LedgeHanging)
        {
            UpdateLedgeHang(dt);
            m_jumpPressedThisFrame = false;
            m_prevJumpHeld = m_jumpHeld;
            return;
        }

        if (m_state == MovementState::LedgeMantling)
        {
            UpdateLedgeMantle(dt);
            m_jumpPressedThisFrame = false;
            m_prevJumpHeld = m_jumpHeld;
            return;
        }

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
#if !defined(NS_SHIPPING)
            // 接地していないのに窓が残って跳べた= コヨーテ窓内ジャンプを debug 記録する
            if (m_debugDraw && !m_isGrounded && m_coyoteTimer > 0.0f)
                PushCoyoteJumpMarker(m_lastGroundedPosition, RootTransform().Position());
#endif
            m_velocity.y = m_jumpImpulse;
            --m_jumpsRemaining;
            m_bufferTimer = 0.0f;
            m_coyoteTimer = 0.0f;
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
        in.physicsWorld = m_world;
        const NS::Physics::CharacterControllerResult out = m_controller.Update(in);

        RootTransform().SetPosition(out.position);
        m_velocity = out.velocity;
        m_wasGrounded = m_isGrounded;
        m_isGrounded = out.grounded;

        if (!m_wasGrounded && m_isGrounded)
            m_jumpsRemaining = 1;

        if (m_isGrounded)
            m_coyoteTimer = m_coyoteTime;

        // 縁を踏み外した瞬間に踏み外し点を保てるよう、 接地している間は最終接地位置を更新し続ける
        if (m_isGrounded)
            m_lastGroundedPosition = out.position;

        // Walking / Jumping / Falling のサブ分類は high-level state の参考にする。 controller bypass はしない
        if (m_isGrounded)
            m_state = MovementState::Walking;
        else if (m_velocity.y > 0.0f)
            m_state = MovementState::Jumping;
        else
            m_state = MovementState::Falling;

        // 通常 block の縁を掴めるか試す。 空中下降中のみ成立する
        TryGrabLedge(out.position);

        m_prevJumpHeld = m_jumpHeld;
        m_jumpPressedThisFrame = false;
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

        for (const NS::Math::AABB& box : WorldAabbs(m_world))
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

            // 上面手前の mantle 先が別 block で塞がっているなら縁ではない。 掴まない
            const float mantleStep = 2.0f * m_capsuleRadius + kLedgeMantleInset;
            const NS::Math::Vector3 mantleCheck{
                hang.x - faceNormal.x * mantleStep,
                top + m_capsuleHalfHeight,
                hang.z - faceNormal.z * mantleStep,
            };
            bool blocked = false;
            for (const NS::Math::AABB& other : WorldAabbs(m_world))
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

        // 登る: jump は即時、 前入力での自動登りは最小ぶら下がり時間だけ一瞬ぶら下がりを見せてから
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

        // それ以外: 縁にぶら下がったまま、 左右入力で縁に沿ってシミー移動する。 重力は無効
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

        for (const NS::Math::AABB& box : WorldAabbs(m_world))
        {
            const float top = box.Center.y + box.Extents.y;
            if (std::abs(top - m_ledgeTopY) > kLedgeContinueTopTol)
                continue;
            if (probe.x < box.Center.x - box.Extents.x || probe.x > box.Center.x + box.Extents.x)
                continue;
            if (probe.z < box.Center.z - box.Extents.z || probe.z > box.Center.z + box.Extents.z)
                continue;

            // 乗り上がり先が別 block で塞がっていたら縁とみなさない。 オーバーハングの下では掴めない
            const float mantleStep = 2.0f * m_capsuleRadius + kLedgeMantleInset;
            const NS::Math::Vector3 mantleCheck{
                hangPos.x - m_ledgeFaceNormal.x * mantleStep,
                m_ledgeTopY + m_capsuleHalfHeight,
                hangPos.z - m_ledgeFaceNormal.z * mantleStep,
            };
            bool blocked = false;
            for (const NS::Math::AABB& other : WorldAabbs(m_world))
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

    NS_REGISTER_COMPONENT(CharacterMovementComponent)
} // namespace NS::Scene
