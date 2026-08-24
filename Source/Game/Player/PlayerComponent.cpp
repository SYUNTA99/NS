#include "Game/Player/PlayerComponent.h"

#include "Game/Entity/EntityStateManagerComponent.h"
#include "Game/Player/PlayerStateManagerComponent.h"
#include "Game/Player/PlayerStatsManagerComponent.h"
#include "Runtime/Object/Components/CameraBrainComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/PhysicsWorld.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace
{
    // 加速と減速の切り替えで見る速さの差の下限。単位は m/s
    constexpr float k_HorizontalSpeedEpsilon = 0.01f;

    // コヨーテジャンプ記録の表示寿命で単位は s。直近の数試行を見比べられる長さ
    constexpr float k_CoyoteJumpMarkerLifetime = 3.0f;
    // 同時に保持するコヨーテジャンプ記録の上限。画面が線で埋まらない数
    constexpr std::size_t k_MaxCoyoteJumpMarkers = 16;

    // 向きと言える長さの下限。長さ 0 のまま正規化すると 0 除算になる
    constexpr float k_BodySlamMinDirection = 1e-4f;

    // 1 歩で打ち切ると衝突を裁く側が突進を見る前に終わるため、壁に押し付けられた歩を 2 回数える
    constexpr float k_BodySlamStallDistance = 1e-4f;
    constexpr int k_BodySlamMaxStallSteps = 2;

    // capsule 上端を手とみなし、block 上端との高さ差の許容下幅 / 上幅で単位は m。この帯に block 上端が
    // 入ると掴める。実機で触って詰める初期値
    constexpr float k_LedgeGrabBandLow = 0.5f;
    constexpr float k_LedgeGrabBandHigh = 0.5f;
    // capsule 表面から前方へ手を伸ばす追加距離で単位は m
    constexpr float k_LedgeReach = 0.3f;
    // よじ登りで面の内側へ押し込む余白で単位は m。2*radius に上乗せして上面へ確実に乗せる
    constexpr float k_LedgeMantleInset = 0.1f;
    // よじ登りの後に block 上面から浮かせる安全マージンで単位は m
    constexpr float k_LedgeMantleLift = 0.02f;
    // よじ登りと手放しを起こす掴まり中の前後入力のしきい値
    constexpr float k_LedgeInputThreshold = 0.5f;
    // 前入力での自動登りを許すまでの最小ぶら下がり時間で単位は s。壁に向かう入力のまま即登り切って
    // 掴まりが見えない問題を防ぐ。ジャンプと手放しはこの待ちを受けない
    constexpr float k_LedgeMinHangTime = 0.3f;
    // ぶら下がりから上面へよじ登る所要時間で単位は s。瞬間移動を避けて登りを視認できるようにする
    constexpr float k_LedgeMantleDuration = 0.25f;
    // 縁に沿った左右移動の速度と入力の遊び。速度の単位は m/s
    constexpr float k_LedgeShimmySpeed = 2.0f;
    constexpr float k_LedgeShimmyDeadzone = 0.3f;
    // シミーの継続判定で同じ高さの縁とみなす上端の許容差で単位は m
    constexpr float k_LedgeContinueTopTol = 0.1f;
    // 手放しで面法線方向へ離す距離と初速で、単位はそれぞれ m と m/s
    constexpr float k_LedgeDropOutward = 0.2f;
    constexpr float k_LedgeDropOutwardSpeed = 2.0f;
    // 手放しとよじ登りの直後に再掴みを禁止する時間で単位は s。放しても入力を倒し続けた時の即再掴みを防ぐ
    constexpr float k_LedgeRegrabCooldownTime = 0.3f;

    // 掴まりの走査が借りる AABB 群。world 未設定なら空を返すので、掴めないだけで落ちない
    [[nodiscard]] std::span<const NS::Core::AABB> WorldAabbs(const NS::Physics::PhysicsWorld* world) noexcept
    {
        if (world == nullptr)
            return {};
        return std::span<const NS::Core::AABB>(world->Aabbs());
    }

    [[nodiscard]] bool AabbContainsPoint(const NS::Core::AABB& box, const NS::Core::Vector3& p) noexcept
    {
        return p.x >= box.Center.x - box.Extents.x && p.x <= box.Center.x + box.Extents.x &&
               p.y >= box.Center.y - box.Extents.y && p.y <= box.Center.y + box.Extents.y &&
               p.z >= box.Center.z - box.Extents.z && p.z <= box.Center.z + box.Extents.z;
    }
} // namespace

namespace NS::Game::Player
{
    const PlayerStats& PlayerComponent::Stats() const noexcept
    {
        if (m_statsManager != nullptr)
            return m_statsManager->Current();

        // 組が無いのはシーンを組まない検証台だけ。既定値は組の既定と同じなので手触りは変わらない
        static const PlayerStats k_Default{};
        return k_Default;
    }

    void PlayerComponent::SetDesiredMove(const NS::Core::Vector3& worldDir, float speedScale01) noexcept
    {
        m_desiredDir = worldDir;
        m_desiredSpeedScale = NS::Core::Clamp(speedScale01, 0.0f, 1.0f);
    }

    void PlayerComponent::SetClimbMove(float localRight, float localForward) noexcept
    {
        m_climbRight = NS::Core::Clamp(localRight, -1.0f, 1.0f);
        m_climbForward = NS::Core::Clamp(localForward, -1.0f, 1.0f);
    }

    void PlayerComponent::SetJumpPressed() noexcept
    {
        m_jumpPressedThisFrame = true;
    }

    void PlayerComponent::SetJumpHeld(bool held) noexcept
    {
        m_jumpHeld = held;
    }

    float PlayerComponent::CoyoteTime() const noexcept
    {
        return Stats().coyoteTime;
    }

    void PlayerComponent::SetMaxSpeed(float speed) noexcept
    {
        // 非有限値は入口で捨てる。CapsuleMover は速度を検査しないので位置まで NaN が伝わる
        if (!std::isfinite(speed))
            return;

        if (speed < 0.0f)
            m_maxSpeed = 0.0f;
        else
            m_maxSpeed = speed;
    }

    void PlayerComponent::RequestBodySlam(float charge01) noexcept
    {
        // その歩で出せないと押しが無言で消える。ジャンプと同じ先行入力時間だけ覚える
        m_bodySlamBufferRemaining = Stats().jumpBufferTime;
        // NaN は 0..1 への丸めを素通りして溜め量に残るため、入口で 0 へ倒す
        if (!std::isfinite(charge01))
            m_bodySlamRequestCharge01 = 0.0f;
        else
            m_bodySlamRequestCharge01 = NS::Core::Clamp(charge01, 0.0f, 1.0f);
    }

    bool PlayerComponent::IsBodySlamming() const noexcept
    {
        return m_stateManager != nullptr && m_stateManager->IsCurrent(k_BodySlamStateName);
    }

    float PlayerComponent::BodySlamProgress01() const noexcept
    {
        if (!IsBodySlamming() || !(m_bodySlamDistanceTarget > 0.0f))
            return 0.0f;
        return NS::Core::Clamp(m_bodySlamTravelled / m_bodySlamDistanceTarget, 0.0f, 1.0f);
    }

    NS::Core::Vector3 PlayerComponent::BodySlamVelocity() const noexcept
    {
        if (!IsBodySlamming())
            return Velocity();

        float speed = Stats().bodySlamSpeed;
        if (m_bodySlamIsTap)
            speed = Stats().tapSlamSpeed;
        return NS::Core::Vector3{m_bodySlamDir.x * speed, VerticalVelocity(), m_bodySlamDir.z * speed};
    }

    void PlayerComponent::CancelBodySlam() noexcept
    {
        if (!IsBodySlamming())
            return;
        m_bodySlamTravelled = 0.0f;
        m_bodySlamDistanceTarget = 0.0f;
        m_stateManager->ChangeByName(k_IdleStateName);
        m_playerEvents.onBodySlamEnded.Invoke();
    }

    bool PlayerComponent::BodySlam() noexcept
    {
        const NS::Core::Vector3 lateral = LateralVelocity();
        NS::Core::Vector3 dir{m_desiredDir.x, 0.0f, m_desiredDir.z};
        float length = std::sqrt(dir.x * dir.x + dir.z * dir.z);

        // 反発後の滑りなど残った速度が向きに勝つと狙いと食い違う方へ飛ぶ。入力が無ければ速度よりカメラの前を先に見る
        if (length < k_BodySlamMinDirection && Owner() != nullptr && Owner()->OwningScene() != nullptr)
        {
            if (NS::Object::CameraBrainComponent* brain = Owner()->OwningScene()->CameraBrain())
            {
                const NS::Core::Vector3 forward = brain->ForwardHorizontal();
                dir = NS::Core::Vector3{forward.x, 0.0f, forward.z};
                length = std::sqrt(dir.x * dir.x + dir.z * dir.z);
            }
        }
        if (length < k_BodySlamMinDirection)
        {
            dir = lateral;
            length = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        }
        if (length < k_BodySlamMinDirection)
            return false;

        dir.x /= length;
        dir.z /= length;
        m_bodySlamDir = dir;
        m_bodySlamEntrySpeed = std::sqrt(lateral.x * lateral.x + lateral.z * lateral.z);
        m_bodySlamCharge01 = m_bodySlamRequestCharge01;
        m_bodySlamIsTap = !(m_bodySlamRequestCharge01 > 0.0f);
        m_bodySlamTravelled = 0.0f;
        m_bodySlamStallSteps = 0;

        if (m_bodySlamIsTap)
        {
            m_bodySlamDistanceTarget = Stats().tapSlamDistance;
            SetVelocity(
                NS::Core::Vector3{dir.x * Stats().tapSlamSpeed, Stats().tapSlamUpSpeed, dir.z * Stats().tapSlamSpeed});
        }
        else
        {
            m_bodySlamDistanceTarget = Stats().bodySlamDistance;
            SetVelocity(
                NS::Core::Vector3{dir.x * Stats().bodySlamSpeed, VerticalVelocity(), dir.z * Stats().bodySlamSpeed});
        }

        // 距離が 0 以下だと 1 歩目で終わって発動が消えるため、出さずに通常移動のままにする
        if (!(m_bodySlamDistanceTarget > 0.0f))
            return false;

        if (m_stateManager != nullptr)
            m_stateManager->ChangeByName(k_BodySlamStateName);
        m_playerEvents.onBodySlamStarted.Invoke();
        return true;
    }

    void PlayerComponent::UpdateBodySlam(float dt) noexcept
    {
        // 突進中に向きを変えられると当てる間合いを詰める意味が消えるので、水平は発動時の値で書き直す
        if (!m_bodySlamIsTap)
        {
            SetLateralVelocity(NS::Core::Vector3{
                m_bodySlamDir.x * Stats().bodySlamSpeed, 0.0f, m_bodySlamDir.z * Stats().bodySlamSpeed});
        }

        Gravity(dt);
        Move(dt);
        SyncGroundState();

        // 進んだ距離は基底が控えた実移動から測る。速度から積むと壁で止められた歩も進んだ扱いになる
        const NS::Core::Vector3 delta = PositionDelta();
        const float stepDistance = std::sqrt(delta.x * delta.x + delta.z * delta.z);
        m_bodySlamTravelled += stepDistance;

        // 壁で止められると距離が減らず突進から出られなくなるため、進めない歩が続いたら打ち切る
        if (stepDistance < k_BodySlamStallDistance)
            ++m_bodySlamStallSteps;
        else
            m_bodySlamStallSteps = 0;

        if (m_bodySlamTravelled >= m_bodySlamDistanceTarget || m_bodySlamStallSteps >= k_BodySlamMaxStallSteps)
        {
            m_bodySlamTravelled = 0.0f;
            m_bodySlamDistanceTarget = 0.0f;
            if (m_stateManager != nullptr)
                m_stateManager->ChangeByName(k_IdleStateName);
            m_playerEvents.onBodySlamEnded.Invoke();
        }
    }

    void PlayerComponent::ResetState() noexcept
    {
        SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
        m_desiredDir = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        m_desiredSpeedScale = 0.0f;
        m_climbRight = 0.0f;
        m_climbForward = 0.0f;
        m_jumpHeld = false;
        m_prevJumpHeld = false;
        m_jumpPressedThisFrame = false;
        m_jumpsRemaining = 1;
        m_coyoteTimer = 0.0f;
        m_bufferTimer = 0.0f;
        SetGrounded(false);
        m_ledgeTopY = 0.0f;
        m_ledgeFaceNormal = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        m_ledgeRegrabCooldown = 0.0f;
        m_ledgeHangTimer = 0.0f;
        m_ledgeMantleTimer = 0.0f;
        m_lastGroundedPosition = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        m_coyoteJumpMarkers.clear();
        m_bodySlamBufferRemaining = 0.0f;
        m_bodySlamIsTap = false;
        m_bodySlamRequestCharge01 = 0.0f;
        m_bodySlamCharge01 = 0.0f;
        m_bodySlamEntrySpeed = 0.0f;
        m_bodySlamTravelled = 0.0f;
        m_bodySlamDistanceTarget = 0.0f;
        m_bodySlamStallSteps = 0;
        m_bodySlamDir = NS::Core::Vector3{0.0f, 0.0f, 0.0f};

        if (m_stateManager != nullptr)
            m_stateManager->ResetToFirst();
    }

    void PlayerComponent::OnStart()
    {
        NS::Game::Entity::EntityComponent::OnStart();

        if (Owner() == nullptr)
            return;

        m_statsManager = Owner()->FindComponent<PlayerStatsManagerComponent>();
        m_stateManager = Owner()->FindComponent<PlayerStateManagerComponent>();
    }

    NS::Game::Entity::EntityStateManagerComponent* PlayerComponent::States() const noexcept
    {
        return m_stateManager;
    }

    void PlayerComponent::TickTimers(float dt) noexcept
    {
        if (m_ledgeRegrabCooldown > 0.0f)
            m_ledgeRegrabCooldown -= dt;

        m_bufferTimer -= dt;
        if (m_jumpPressedThisFrame)
            m_bufferTimer = Stats().jumpBufferTime;

        const bool inAir = !IsGrounded();
        if (inAir)
            m_coyoteTimer -= dt;
    }

    void PlayerComponent::AccelerateToInputDirection(float dt) noexcept
    {
        float targetSpeed = 0.0f;
        if (m_desiredSpeedScale >= Stats().stickDeadzone)
        {
            if (m_desiredSpeedScale < 0.5f)
                targetSpeed = Stats().walkSpeed;
            else
                targetSpeed = m_maxSpeed * m_desiredSpeedScale;
        }

        const NS::Core::Vector3 targetHoriz{m_desiredDir.x * targetSpeed, 0.0f, m_desiredDir.z * targetSpeed};

        // 目標が今の速さを上回る歩だけ加速の時定数。誤差ぶんの差で加速と減速が入れ替わらないよう下駄を履かせる
        const NS::Core::Vector3 lateral = LateralVelocity();
        const float currHorizMag = std::sqrt(lateral.x * lateral.x + lateral.z * lateral.z);
        float tau = Stats().decelTau;
        if (targetSpeed > currHorizMag + k_HorizontalSpeedEpsilon)
            tau = Stats().accelTau;

        Accelerate(targetHoriz, tau, dt);
    }

    void PlayerComponent::Jump(float) noexcept
    {
        const bool canGroundJump = (IsGrounded() || m_coyoteTimer > 0.0f) && m_jumpsRemaining > 0;
        const bool wantJump = m_jumpPressedThisFrame || m_bufferTimer > 0.0f;
        if (canGroundJump && wantJump)
        {
#if !defined(NS_SHIPPING)
            // 接地していないのに窓が残って跳べた = コヨーテ窓内ジャンプなので記録する
            if (m_debugDraw && !IsGrounded() && m_coyoteTimer > 0.0f)
                PushCoyoteJumpMarker(m_lastGroundedPosition, RootTransform().Position());
#endif
            SetVerticalVelocity(Stats().jumpImpulse);
            --m_jumpsRemaining;
            m_bufferTimer = 0.0f;
            m_coyoteTimer = 0.0f;
            m_playerEvents.onJump.Invoke();
        }
    }

    void PlayerComponent::CutJumpRelease() noexcept
    {
        if (m_prevJumpHeld && !m_jumpHeld && VerticalVelocity() > 0.0f)
            SetVerticalVelocity(VerticalVelocity() * Stats().jumpReleaseScale);
    }

    void PlayerComponent::Gravity(float dt) noexcept
    {
        const bool apex = std::abs(VerticalVelocity()) < Stats().apexHangVy;

        float baseG = Stats().gravityDown;
        if (VerticalVelocity() > 0.0f)
            baseG = Stats().gravityUp;

        float g = baseG;
        if (apex)
            g = baseG * Stats().apexHangScale;

        NS::Game::Entity::EntityComponent::Gravity(g, dt);
    }

    void PlayerComponent::SyncGroundState() noexcept
    {
        if (!WasGrounded() && IsGrounded())
            m_jumpsRemaining = 1;

        // 縁を踏み外した瞬間に踏み外し点を保てるよう、接地している間は最終接地位置を張り直し続ける
        if (IsGrounded())
        {
            m_coyoteTimer = CoyoteTime();
            m_lastGroundedPosition = RootTransform().Position();
        }
    }

    bool PlayerComponent::LedgeGrab() noexcept
    {
        // 空中で下降中、かつ前入力がある時だけ掴む。再掴み禁止の間は無効
        if (m_ledgeRegrabCooldown > 0.0f || IsGrounded() || VerticalVelocity() > 0.0f)
            return false;
        if (m_desiredSpeedScale <= Stats().stickDeadzone)
            return false;

        NS::Core::Vector3 dir{m_desiredDir.x, 0.0f, m_desiredDir.z};
        const float dirLen = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        if (dirLen < 1e-4f)
            return false;
        dir.x /= dirLen;
        dir.z /= dirLen;

        // 手の高さ = capsule 上端。そこから前方へ伸ばした probe 点が block の XZ 内に入り、
        // かつ block 上端が手の高さの帯に収まれば縁とみなす
        const NS::Core::Vector3 pos = RootTransform().Position();
        const float handY = pos.y + CapsuleHalfHeight();
        const NS::Core::Vector3 probe{
            pos.x + dir.x * (CapsuleRadius() + k_LedgeReach),
            handY,
            pos.z + dir.z * (CapsuleRadius() + k_LedgeReach),
        };

        for (const NS::Core::AABB& box : WorldAabbs(PhysicsWorld()))
        {
            const float top = box.Center.y + box.Extents.y;
            if (top < handY - k_LedgeGrabBandLow || top > handY + k_LedgeGrabBandHigh)
                continue;
            if (probe.x < box.Center.x - box.Extents.x || probe.x > box.Center.x + box.Extents.x)
                continue;
            if (probe.z < box.Center.z - box.Extents.z || probe.z > box.Center.z + box.Extents.z)
                continue;

            // 接近軸の優勢成分で掴む手前面を決め、その外側に capsule を寄せた hang 位置を出す
            NS::Core::Vector3 faceNormal{0.0f, 0.0f, 0.0f};
            NS::Core::Vector3 hang = pos;
            if (std::abs(dir.x) >= std::abs(dir.z))
            {
                float sgn = -1.0f;
                if (dir.x >= 0.0f)
                    sgn = 1.0f;
                const float faceX = box.Center.x - sgn * box.Extents.x;
                faceNormal = NS::Core::Vector3{-sgn, 0.0f, 0.0f};
                hang.x = faceX - sgn * CapsuleRadius();
                hang.z = NS::Core::Clamp(pos.z, box.Center.z - box.Extents.z, box.Center.z + box.Extents.z);
            }
            else
            {
                float sgn = -1.0f;
                if (dir.z >= 0.0f)
                    sgn = 1.0f;
                const float faceZ = box.Center.z - sgn * box.Extents.z;
                faceNormal = NS::Core::Vector3{0.0f, 0.0f, -sgn};
                hang.z = faceZ - sgn * CapsuleRadius();
                hang.x = NS::Core::Clamp(pos.x, box.Center.x - box.Extents.x, box.Center.x + box.Extents.x);
            }
            hang.y = top - CapsuleHalfHeight();

            // 上面手前の登り先が別 block で塞がっているなら縁ではない。掴まない
            const float mantleStep = 2.0f * CapsuleRadius() + k_LedgeMantleInset;
            const NS::Core::Vector3 mantleCheck{
                hang.x - faceNormal.x * mantleStep,
                top + CapsuleHalfHeight(),
                hang.z - faceNormal.z * mantleStep,
            };
            bool blocked = false;
            for (const NS::Core::AABB& other : WorldAabbs(PhysicsWorld()))
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
            SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
            m_ledgeTopY = top;
            m_ledgeFaceNormal = faceNormal;
            m_ledgeHangTimer = 0.0f;
            if (m_stateManager != nullptr)
                m_stateManager->ChangeByName(k_LedgeHangingStateName);
            m_playerEvents.onLedgeGrabbed.Invoke();
            return true;
        }
        return false;
    }

    void PlayerComponent::HoldLedge(float dt) noexcept
    {
        m_ledgeHangTimer += dt;

        NS::Core::Vector3 pos = RootTransform().Position();
        pos.y = m_ledgeTopY - CapsuleHalfHeight();
        RootTransform().SetPosition(pos);
        SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
    }

    bool PlayerComponent::ShouldClimbLedge() const noexcept
    {
        const bool autoClimb = m_climbForward > k_LedgeInputThreshold && m_ledgeHangTimer >= k_LedgeMinHangTime;
        return m_jumpPressedThisFrame || autoClimb;
    }

    bool PlayerComponent::ShouldDropLedge() const noexcept
    {
        return m_climbForward < -k_LedgeInputThreshold;
    }

    void PlayerComponent::ClimbLedge() noexcept
    {
        const NS::Core::Vector3 pos = RootTransform().Position();
        const float mantleStep = 2.0f * CapsuleRadius() + k_LedgeMantleInset;
        m_ledgeMantleStart = pos;
        m_ledgeMantleEnd = NS::Core::Vector3{
            pos.x - m_ledgeFaceNormal.x * mantleStep,
            m_ledgeTopY + CapsuleHalfHeight() + CapsuleRadius() + k_LedgeMantleLift,
            pos.z - m_ledgeFaceNormal.z * mantleStep,
        };
        m_ledgeMantleTimer = 0.0f;
        if (m_stateManager != nullptr)
            m_stateManager->ChangeByName(k_LedgeClimbingStateName);
        SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
        m_playerEvents.onLedgeClimbing.Invoke();
    }

    void PlayerComponent::DropLedge() noexcept
    {
        NS::Core::Vector3 pos = RootTransform().Position();
        pos.x += m_ledgeFaceNormal.x * k_LedgeDropOutward;
        pos.z += m_ledgeFaceNormal.z * k_LedgeDropOutward;
        RootTransform().SetPosition(pos);
        if (m_stateManager != nullptr)
            m_stateManager->ChangeByName(k_IdleStateName);
        SetVelocity(NS::Core::Vector3{
            m_ledgeFaceNormal.x * k_LedgeDropOutwardSpeed, 0.0f, m_ledgeFaceNormal.z * k_LedgeDropOutwardSpeed});
        SetGrounded(false);
        m_ledgeRegrabCooldown = k_LedgeRegrabCooldownTime;
    }

    void PlayerComponent::Shimmy(float dt) noexcept
    {
        if (std::abs(m_climbRight) > k_LedgeShimmyDeadzone)
        {
            // 面法線に水平直交する縁方向。動いても面からの距離は変わらない
            const NS::Core::Vector3 pos = RootTransform().Position();
            const NS::Core::Vector3 alongDir{-m_ledgeFaceNormal.z, 0.0f, m_ledgeFaceNormal.x};
            NS::Core::Vector3 shimmied = pos;
            shimmied.x += alongDir.x * m_climbRight * k_LedgeShimmySpeed * dt;
            shimmied.z += alongDir.z * m_climbRight * k_LedgeShimmySpeed * dt;
            // 移動先にも同じ高さの縁が続いている時だけ動く。端なら止めて落とさない
            if (CanShimmyTo(shimmied))
                RootTransform().SetPosition(shimmied);
        }
    }

    void PlayerComponent::UpdateLedgeClimb(float dt) noexcept
    {
        m_ledgeMantleTimer += dt;
        const float t = NS::Core::Clamp(m_ledgeMantleTimer / k_LedgeMantleDuration, 0.0f, 1.0f);

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
        RootTransform().SetPosition(pos);
        SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});

        if (t >= 1.0f)
        {
            RootTransform().SetPosition(m_ledgeMantleEnd);
            if (m_stateManager != nullptr)
                m_stateManager->ChangeByName(k_IdleStateName);
            SetGrounded(true);
            m_jumpsRemaining = 1;
            m_coyoteTimer = CoyoteTime();
            m_ledgeRegrabCooldown = k_LedgeRegrabCooldownTime;
        }
    }

    bool PlayerComponent::CanShimmyTo(const NS::Core::Vector3& hangPos) const noexcept
    {
        const NS::Core::Vector3 inward{-m_ledgeFaceNormal.x, 0.0f, -m_ledgeFaceNormal.z};
        const float handY = hangPos.y + CapsuleHalfHeight();
        const NS::Core::Vector3 probe{
            hangPos.x + inward.x * (CapsuleRadius() + k_LedgeReach),
            handY,
            hangPos.z + inward.z * (CapsuleRadius() + k_LedgeReach),
        };

        for (const NS::Core::AABB& box : WorldAabbs(PhysicsWorld()))
        {
            const float top = box.Center.y + box.Extents.y;
            if (std::abs(top - m_ledgeTopY) > k_LedgeContinueTopTol)
                continue;
            if (probe.x < box.Center.x - box.Extents.x || probe.x > box.Center.x + box.Extents.x)
                continue;
            if (probe.z < box.Center.z - box.Extents.z || probe.z > box.Center.z + box.Extents.z)
                continue;

            // 乗り上がり先が別 block で塞がっていたら縁とみなさない。オーバーハングの下では掴めない
            const float mantleStep = 2.0f * CapsuleRadius() + k_LedgeMantleInset;
            const NS::Core::Vector3 mantleCheck{
                hangPos.x - m_ledgeFaceNormal.x * mantleStep,
                m_ledgeTopY + CapsuleHalfHeight(),
                hangPos.z - m_ledgeFaceNormal.z * mantleStep,
            };
            bool blocked = false;
            for (const NS::Core::AABB& other : WorldAabbs(PhysicsWorld()))
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

    // 綴りが状態側とずれると突進が出なくなるので、走りと落下は player_state_registration_test が
    // 同じ名前で登録簿から作れることを見張っている
    bool PlayerComponent::IsLocomotion() const noexcept
    {
        if (m_stateManager == nullptr)
            return false;
        return m_stateManager->IsCurrent(k_IdleStateName) || m_stateManager->IsCurrent("Walk") ||
               m_stateManager->IsCurrent("Fall");
    }

    bool PlayerComponent::ShouldWalk() const noexcept
    {
        if (!IsGrounded())
            return false;
        if (m_desiredSpeedScale >= Stats().stickDeadzone)
            return true;

        const NS::Core::Vector3 lateral = LateralVelocity();
        return std::sqrt(lateral.x * lateral.x + lateral.z * lateral.z) > k_HorizontalSpeedEpsilon;
    }

    bool PlayerComponent::ShouldIdle() const noexcept
    {
        return IsGrounded() && !ShouldWalk();
    }

    bool PlayerComponent::ShouldFall() const noexcept
    {
        return !IsGrounded();
    }

    void PlayerComponent::PushCoyoteJumpMarker(const NS::Core::Vector3& edge, const NS::Core::Vector3& jump) noexcept
    {
        if (m_coyoteJumpMarkers.size() >= k_MaxCoyoteJumpMarkers)
            m_coyoteJumpMarkers.erase(m_coyoteJumpMarkers.begin());
        m_coyoteJumpMarkers.push_back(CoyoteJumpMarker{edge, jump, k_CoyoteJumpMarkerLifetime});
    }

    void PlayerComponent::HandleStates(float dt)
    {
#if !defined(NS_SHIPPING)
        // 掴まりの状態は移動の 1 歩を通らないので、記録の減衰は状態の外に置く
        for (CoyoteJumpMarker& marker : m_coyoteJumpMarkers)
            marker.remaining -= dt;
        std::erase_if(m_coyoteJumpMarkers, [](const CoyoteJumpMarker& m) { return m.remaining <= 0.0f; });
#endif

        if (m_stateManager != nullptr)
        {
            // 発動の判定が現在状態を見るので、組むのは 1 歩の頭。Step の初回に任せると
            // 1 歩目だけ現在状態が空になり、その歩の押しが落ちる
            m_stateManager->EnsureBuilt(*this);

            // 突進の中で見ると通常移動の 1 歩を走ってから移ることになり、突進の初速がその歩に乗らない
            // 空中の押しを捨てると連打で出ない歩ができるため、接地は求めない
            // 突進を出すのは通常移動の歩だけ。掴まり中に出せると縁から離れる操作が 3 通りになる
            if (m_bodySlamBufferRemaining > 0.0f && IsLocomotion())
            {
                if (BodySlam())
                    m_bodySlamBufferRemaining = 0.0f;
            }

            m_stateManager->Step(*this, dt);
        }

        // 1 歩限りの入力の消費は、どの状態でも通るここで行う
        m_prevJumpHeld = m_jumpHeld;
        m_jumpPressedThisFrame = false;
        if (m_bodySlamBufferRemaining > 0.0f)
            m_bodySlamBufferRemaining = std::max(0.0f, m_bodySlamBufferRemaining - dt);
    }

    void PlayerComponent::OnStepSkipped()
    {
        m_jumpPressedThisFrame = false;
        m_prevJumpHeld = m_jumpHeld;
    }

    NS_CLASS(PlayerComponent)
} // namespace NS::Game::Player
