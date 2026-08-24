#include "Game/Player/PlayerComponent.h"

#include "Game/Entity/EntityStateManagerComponent.h"
#include "Game/Player/PlayerStatsManagerComponent.h"
#include "Runtime/Object/Components/CameraBrainComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

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
        return true;
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
        m_stateManager = Owner()->FindComponent<NS::Game::Entity::EntityStateManagerComponent>();
    }

    void PlayerComponent::TickTimers(float dt) noexcept
    {
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
        // TODO: 縁の探索は掴まりを移す時に足す。今はどこでも掴めない
        return false;
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

    // TODO: 状態機械へ差し替えるまでの 1 本道。並びを変えると手触りが変わるので、上から下をそのまま保つ
    void PlayerComponent::StepLocomotion(float dt) noexcept
    {
        TickTimers(dt);
        AccelerateToInputDirection(dt);
        Jump(dt);
        CutJumpRelease();
        Gravity(dt);
        Move(dt);
        SyncGroundState();
        if (LedgeGrab())
            return;
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

        // 突進の中で見ると通常移動の 1 歩を走ってから移ることになり、突進の初速がその歩に乗らない
        // 空中の押しを捨てると連打で出ない歩ができるため、接地は求めない
        if (m_bodySlamBufferRemaining > 0.0f && !IsBodySlamming())
        {
            if (BodySlam())
                m_bodySlamBufferRemaining = 0.0f;
        }

        // TODO: 突進の 1 歩を足すまでの仮。通常移動を走らせると発動時の速度が上書きされる
        if (!IsBodySlamming())
            StepLocomotion(dt);

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
