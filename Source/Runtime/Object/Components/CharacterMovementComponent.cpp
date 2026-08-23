#include "Runtime/Object/Components/CharacterMovementComponent.h"

#include "Runtime/Core/Clock.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Object/Components/CameraBrainComponent.h"
#include "Runtime/Object/Components/CapsuleColliderComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/PhysicsWorld.h"

#include <cmath>
#include <string_view>

namespace
{
    constexpr float k_HorizontalSpeedEpsilon = 0.01f;

    // 向きと言える長さの下限。長さ 0 のまま正規化すると 0 除算になる
    constexpr float k_BodySlamMinDirection = 1e-4f;

    // 1 歩で打ち切ると衝突を裁く側が突進を見る前に終わるため、壁に押し付けられた歩を 2 回数える
    constexpr float k_BodySlamStallDistance = 1e-4f;
    constexpr int k_BodySlamMaxStallSteps = 2;

    //! ledge grab が走査する AABB 群を world から借りる。 world 未設定時は空で掴めない
    [[nodiscard]] std::span<const NS::Core::AABB> WorldAabbs(const NS::Physics::PhysicsWorld* world) noexcept
    {
        if (world == nullptr)
            return {};
        return std::span<const NS::Core::AABB>(world->Aabbs());
    }

    //! 一次遅れの離散化。tau は時定数で値が大きいほど鈍い、dt は step。0 < tau で安定
    [[nodiscard]] float SmoothApproach(float current, float target, float tau, float dt) noexcept
    {
        if (tau <= 0.0f)
            return target;
        const float a = 1.0f - std::exp(-dt / tau);
        return current + (target - current) * a;
    }

    [[nodiscard]] NS::Core::Vector3 HorizontalSmooth(const NS::Core::Vector3& curr,
                                                     const NS::Core::Vector3& target,
                                                     float tau,
                                                     float dt) noexcept
    {
        return NS::Core::Vector3{
            SmoothApproach(curr.x, target.x, tau, dt),
            curr.y,
            SmoothApproach(curr.z, target.z, tau, dt),
        };
    }

    //! 縁掴み: capsule 上端を手とみなし、 block 上端との高さ差の許容下幅 / 上幅で単位は m。 この帯に
    //! block 上端が入ると掴める。 GUI playtest で詰める初期値
    constexpr float k_LedgeGrabBandLow = 0.5f;
    constexpr float k_LedgeGrabBandHigh = 0.5f;
    //! 縁掴み: capsule 表面から前方へ手を伸ばす追加距離。 単位は m
    constexpr float k_LedgeReach = 0.3f;
    //! 縁掴み: mantle 時に面の内側へ押し込む余白で単位は m。 2*radius に上乗せして上面へ確実に乗せる
    constexpr float k_LedgeMantleInset = 0.1f;
    //! 縁掴み: mantle 後に block 上面から浮かせる安全マージンで単位は m
    constexpr float k_LedgeMantleLift = 0.02f;
    //! 縁掴み: mantle / drop を起動する climb 前後入力のしきい値
    constexpr float k_LedgeInputThreshold = 0.5f;
    //! 縁掴み: つかんだ後、 前入力での自動登りを許すまでの最小ぶら下がり時間で単位は s。 壁に向かう
    //! 入力のまま即登り切ってつかみが見えない問題を防ぐ。 jump / drop はこの待ちを受けない
    constexpr float k_LedgeMinHangTime = 0.3f;

    //! コヨーテジャンプ記録の表示寿命で単位は s。 直近の数試行を見比べられる長さ
    constexpr float k_CoyoteJumpMarkerLifetime = 3.0f;
    //! 同時に保持するコヨーテジャンプ記録の上限。 画面が赤線で埋まらない数
    constexpr std::size_t k_MaxCoyoteJumpMarkers = 16;
    //! 縁掴み: ぶら下がりから上面へよじ登る mantle モーションの所要時間で単位は s。 瞬間移動を避けて
    //! 登りを視認できるようにする。 前半で上昇、 後半で前進の 2 段に割る
    constexpr float k_LedgeMantleDuration = 0.25f;
    //! 縁掴み: 縁に沿った左右移動シミーの速度と入力デッドゾーン、 速度の単位は m/s
    constexpr float k_LedgeShimmySpeed = 2.0f;
    constexpr float k_LedgeShimmyDeadzone = 0.3f;
    //! 縁掴み: シミー継続判定で「同じ高さの縁」とみなす上端の許容差。 単位は m
    constexpr float k_LedgeContinueTopTol = 0.1f;
    //! 縁掴み: drop 時に面法線方向へ離す距離と初速で、 単位はそれぞれ m と m/s
    constexpr float k_LedgeDropOutward = 0.2f;
    constexpr float k_LedgeDropOutwardSpeed = 2.0f;
    //! 縁掴み: drop / mantle 直後に再掴みを禁止する時間で単位は s。 放しても入力を倒し続けた時の
    //! 即再掴みを防ぐ
    constexpr float k_LedgeRegrabCooldownTime = 0.3f;

    [[nodiscard]] bool AabbContainsPoint(const NS::Core::AABB& box, const NS::Core::Vector3& p) noexcept
    {
        return p.x >= box.Center.x - box.Extents.x && p.x <= box.Center.x + box.Extents.x &&
               p.y >= box.Center.y - box.Extents.y && p.y <= box.Center.y + box.Extents.y &&
               p.z >= box.Center.z - box.Extents.z && p.z <= box.Center.z + box.Extents.z;
    }
} // namespace

namespace NS::Object
{
    //! 通常移動。歩き / ジャンプ / 落下を 1 本の物理パイプラインで進める
    class LocomotionState final : public State<CharacterMovementComponent>
    {
    public:
        static constexpr const char* k_Name = "Locomotion";
        [[nodiscard]] const char* Name() const noexcept override { return k_Name; }
        void OnStep(CharacterMovementComponent& owner, float dt) override { owner.UpdateLocomotion(dt); }
    };
    NS_STATE(LocomotionState, CharacterMovementComponent)

    //! 縁ぶら下がり。controller を通さず position を直更新する
    class LedgeHangState final : public State<CharacterMovementComponent>
    {
    public:
        static constexpr const char* k_Name = "LedgeHang";
        [[nodiscard]] const char* Name() const noexcept override { return k_Name; }
        void OnStep(CharacterMovementComponent& owner, float dt) override { owner.UpdateLedgeHang(dt); }
    };
    NS_STATE(LedgeHangState, CharacterMovementComponent)

    //! 縁のよじ登り。2 段補間で上面へ移動する
    class LedgeMantleState final : public State<CharacterMovementComponent>
    {
    public:
        static constexpr const char* k_Name = "LedgeMantle";
        [[nodiscard]] const char* Name() const noexcept override { return k_Name; }
        void OnStep(CharacterMovementComponent& owner, float dt) override { owner.UpdateLedgeMantle(dt); }
    };
    NS_STATE(LedgeMantleState, CharacterMovementComponent)

    //! 体当たりの突進。発動時に固定した向きへ、距離を使い切るまで進む
    class BodySlamState final : public State<CharacterMovementComponent>
    {
    public:
        static constexpr const char* k_Name = "BodySlam";
        [[nodiscard]] const char* Name() const noexcept override { return k_Name; }
        void OnStep(CharacterMovementComponent& owner, float dt) override { owner.UpdateBodySlam(dt); }
    };
    NS_STATE(BodySlamState, CharacterMovementComponent)

    CharacterMovementComponent::CharacterMovementComponent() noexcept {}

    void CharacterMovementComponent::SetDesiredMove(const NS::Core::Vector3& worldDir, float speedScale01) noexcept
    {
        m_desiredDir = worldDir;
        m_desiredSpeedScale = NS::Core::Clamp(speedScale01, 0.0f, 1.0f);
    }

    void CharacterMovementComponent::SetMaxSpeed(float speed) noexcept
    {
        // 非有限値は入口で捨てる。CapsuleMover は速度を検査しないので位置まで NaN が伝わる
        if (!std::isfinite(speed))
            return;

        if (speed < 0.0f)
            m_maxSpeed = 0.0f;
        else
            m_maxSpeed = speed;
    }

    void CharacterMovementComponent::SetClimbMove(float localRight, float localForward) noexcept
    {
        m_climbRight = NS::Core::Clamp(localRight, -1.0f, 1.0f);
        m_climbForward = NS::Core::Clamp(localForward, -1.0f, 1.0f);
    }

    void CharacterMovementComponent::SetJumpPressed() noexcept
    {
        m_jumpPressedThisFrame = true;
    }

    void CharacterMovementComponent::SetJumpHeld(bool held) noexcept
    {
        m_jumpHeld = held;
    }

    void CharacterMovementComponent::RequestBodySlam(float charge01) noexcept
    {
        // その歩で出せないと押しが無言で消える。ジャンプと同じ先行入力時間だけ覚える
        m_bodySlamBufferRemaining = m_jumpBufferTime;
        // NaN は 0..1 への丸めを素通りして溜め量に残るため、入口で 0 へ倒す
        if (!std::isfinite(charge01))
            m_bodySlamRequestCharge01 = 0.0f;
        else
            m_bodySlamRequestCharge01 = NS::Core::Clamp(charge01, 0.0f, 1.0f);
    }

    bool CharacterMovementComponent::IsBodySlamming() const noexcept
    {
        return m_machine.IsBuilt() && std::string_view(m_machine.CurrentName()) == BodySlamState::k_Name;
    }

    float CharacterMovementComponent::BodySlamProgress01() const noexcept
    {
        if (!IsBodySlamming() || !(m_bodySlamDistanceTarget > 0.0f))
            return 0.0f;
        return NS::Core::Clamp(m_bodySlamTravelled / m_bodySlamDistanceTarget, 0.0f, 1.0f);
    }

    NS::Core::Vector3 CharacterMovementComponent::BodySlamVelocity() const noexcept
    {
        if (!IsBodySlamming())
            return m_velocity;
        float speed = m_bodySlamSpeed;
        if (m_bodySlamIsTap)
            speed = m_tapSlamSpeed;
        return NS::Core::Vector3{m_bodySlamDir.x * speed, m_velocity.y, m_bodySlamDir.z * speed};
    }

    void CharacterMovementComponent::CancelBodySlam() noexcept
    {
        if (!IsBodySlamming())
            return;
        m_bodySlamTravelled = 0.0f;
        m_bodySlamDistanceTarget = 0.0f;
        m_machine.Change(*this, LocomotionState::k_Name);
    }

    void CharacterMovementComponent::ResetState() noexcept
    {
        m_velocity = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
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
        m_wasGrounded = false;
        m_isGrounded = false;
        m_state = MovementState::Walking;
        m_machine.Reset();
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
        m_bodySlamDir = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
    }

    void CharacterMovementComponent::OnStart()
    {
        if (m_world == nullptr && Owner() != nullptr && Owner()->OwningScene() != nullptr)
            m_world = &Owner()->OwningScene()->Physics();
        if (Owner() != nullptr)
            m_capsuleCollider = Owner()->FindComponent<CapsuleColliderComponent>();
    }

    void CharacterMovementComponent::PushCoyoteJumpMarker(const NS::Core::Vector3& edge,
                                                          const NS::Core::Vector3& jump) noexcept
    {
        if (m_coyoteJumpMarkers.size() >= k_MaxCoyoteJumpMarkers)
            m_coyoteJumpMarkers.erase(m_coyoteJumpMarkers.begin());
        m_coyoteJumpMarkers.push_back(CoyoteJumpMarker{edge, jump, k_CoyoteJumpMarkerLifetime});
    }

    void CharacterMovementComponent::OnUpdate()
    {
        NS_SCOPED_TIMER(Game, "CharacterMovement::OnUpdate");

        // 当たりの形の正は同居する CapsuleColliderComponent。写さないと Inspector で触っても移動に効かない
        if (m_capsuleCollider != nullptr)
        {
            m_capsuleRadius = m_capsuleCollider->Radius();
            m_capsuleHalfHeight = m_capsuleCollider->HalfHeight();
        }

        const float dt = NS::Core::FrameTimer::FixedDelta();

        if (!IsActive() || dt <= 0.0f)
        {
            m_jumpPressedThisFrame = false;
            m_prevJumpHeld = m_jumpHeld;
            return;
        }

#if !defined(NS_SHIPPING)
        // コヨーテジャンプ記録を寿命で減衰させる。 ledge 系の状態は UpdateLocomotion を通らないので状態機械の外に置く
        for (CoyoteJumpMarker& marker : m_coyoteJumpMarkers)
            marker.remaining -= dt;
        std::erase_if(m_coyoteJumpMarkers, [](const CoyoteJumpMarker& m) { return m.remaining <= 0.0f; });
#endif

        // 状態一覧はデータ (States) から組む。 初回だけ組み、 以降は現在状態が 1 歩を進める
        if (!m_machine.IsBuilt())
            BuildStates();

        // 状態の中で見ると Locomotion の 1 歩を走ってから移ることになり、突進の初速がその歩に乗らない
        // 空中の押しを捨てると連打で出ない時ができるため、接地は求めない
        if (m_bodySlamBufferRemaining > 0.0f && std::string_view(m_machine.CurrentName()) == LocomotionState::k_Name)
        {
            if (BeginBodySlam())
                m_bodySlamBufferRemaining = 0.0f;
        }

        m_machine.Step(*this, dt);

        // 1 フレームだけ有効な入力フラグの消費は、 どの状態でも通るここで行う
        m_prevJumpHeld = m_jumpHeld;
        m_jumpPressedThisFrame = false;
        if (m_bodySlamBufferRemaining > 0.0f)
            m_bodySlamBufferRemaining = std::max(0.0f, m_bodySlamBufferRemaining - dt);
    }

    void CharacterMovementComponent::BuildStates()
    {
        // "A;B;C" をセミコロンで割る。 空要素は捨てる
        std::vector<std::string> names;
        std::size_t pos = 0;
        while (pos < m_stateNames.size())
        {
            std::size_t end = m_stateNames.find(';', pos);
            if (end == std::string::npos)
                end = m_stateNames.size();
            if (end > pos)
                names.push_back(m_stateNames.substr(pos, end - pos));
            pos = end + 1;
        }

        if (m_machine.Build(*this, names))
            return;
        if (m_machine.IsBuilt())
        {
            NS_LOG_ERROR(Game, "CharacterMovement: States に未登録名があり飛ばした: {}", m_stateNames);
            return;
        }
        // 全滅なら既定の並びで動かす。 データ不備で移動が止まる事故を避ける
        NS_LOG_ERROR(Game, "CharacterMovement: States が組めないため既定の並びへ退避: {}", m_stateNames);
        const std::vector<std::string> fallback = {
            LocomotionState::k_Name, LedgeHangState::k_Name, LedgeMantleState::k_Name, BodySlamState::k_Name};
        m_machine.Build(*this, fallback);
    }

    void CharacterMovementComponent::UpdateLocomotion(float dt) noexcept
    {
        // 各種タイマーの更新
        if (m_ledgeRegrabCooldown > 0.0f)
            m_ledgeRegrabCooldown -= dt;

        m_bufferTimer -= dt;
        if (m_jumpPressedThisFrame)
            m_bufferTimer = m_jumpBufferTime;

        const bool inAir = !m_isGrounded;
        if (inAir)
            m_coyoteTimer -= dt;

        // 目標速度の決定と水平速度の平滑化
        float targetSpeed = 0.0f;
        if (m_desiredSpeedScale >= m_stickDeadzone)
        {
            if (m_desiredSpeedScale < 0.5f)
                targetSpeed = m_walkSpeed;
            else
                targetSpeed = m_maxSpeed * m_desiredSpeedScale;
        }

        NS::Core::Vector3 targetHoriz{m_desiredDir.x * targetSpeed, 0.0f, m_desiredDir.z * targetSpeed};

        const float currHorizMag = std::sqrt(m_velocity.x * m_velocity.x + m_velocity.z * m_velocity.z);
        const float tau = [&]() -> float {
            if (targetSpeed > currHorizMag + k_HorizontalSpeedEpsilon)
                return m_accelTau;
            return m_decelTau;
        }();
        m_velocity = HorizontalSmooth(m_velocity, targetHoriz, tau, dt);

        // 接地 / コヨーテ窓でのジャンプ発火
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

        // ボタンリリース時の上昇カット
        if (m_prevJumpHeld && !m_jumpHeld && m_velocity.y > 0.0f)
            m_velocity.y *= m_jumpReleaseScale;

        // 上昇下降で非対称な重力と apex 付近の滞空
        const bool apex = std::abs(m_velocity.y) < m_apexHangVy;
        const float baseG = [&]() -> float {
            if (m_velocity.y > 0.0f)
                return m_gravityUp;
            return m_gravityDown;
        }();
        const float g = [&]() -> float {
            if (apex)
                return baseG * m_apexHangScale;
            return baseG;
        }();
        m_velocity.y += g * dt;

        // controller での衝突解決と結果反映
        NS::Physics::CapsuleMoverInput in{};
        in.position = RootTransform().Position();
        in.velocity = m_velocity;
        in.dt = dt;
        in.capsuleRadius = m_capsuleRadius;
        in.capsuleHalfHeight = m_capsuleHalfHeight;
        in.physicsWorld = m_world;
        const NS::Physics::CapsuleMoverResult out = m_controller.Update(in);

        RootTransform().SetPosition(out.position);
        m_velocity = out.velocity;
        m_wasGrounded = m_isGrounded;
        m_isGrounded = out.grounded;

        // 着地でのジャンプ回数リセットとコヨーテ猶予の張り直し
        if (!m_wasGrounded && m_isGrounded)
            m_jumpsRemaining = 1;

        if (m_isGrounded)
            m_coyoteTimer = m_coyoteTime;

        // 縁を踏み外した瞬間に踏み外し点を保てるよう、 接地している間は最終接地位置を更新し続ける
        if (m_isGrounded)
            m_lastGroundedPosition = out.position;

        // Walking / Jumping / Falling のサブ分類は 上位状態の参考にする。 controller は通したまま
        if (m_isGrounded)
            m_state = MovementState::Walking;
        else if (m_velocity.y > 0.0f)
            m_state = MovementState::Jumping;
        else
            m_state = MovementState::Falling;

        // 通常 block の縁を掴めるか試す。 空中下降中のみ成立する
        TryGrabLedge(out.position);
    }

    bool CharacterMovementComponent::BeginBodySlam() noexcept
    {
        NS::Core::Vector3 dir{m_desiredDir.x, 0.0f, m_desiredDir.z};
        float length = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        // 反発後の滑りなど残った速度が向きに勝つと狙いと食い違う方へ飛ぶ。入力が無ければ速度よりカメラの前を先に見る
        if (length < k_BodySlamMinDirection && Owner() != nullptr && Owner()->OwningScene() != nullptr)
        {
            if (CameraBrainComponent* brain = Owner()->OwningScene()->CameraBrain())
            {
                const NS::Core::Vector3 forward = brain->ForwardHorizontal();
                dir = NS::Core::Vector3{forward.x, 0.0f, forward.z};
                length = std::sqrt(dir.x * dir.x + dir.z * dir.z);
            }
        }
        if (length < k_BodySlamMinDirection)
        {
            dir = NS::Core::Vector3{m_velocity.x, 0.0f, m_velocity.z};
            length = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        }
        if (length < k_BodySlamMinDirection)
            return false;

        dir.x /= length;
        dir.z /= length;
        m_bodySlamDir = dir;
        m_bodySlamEntrySpeed = std::sqrt(m_velocity.x * m_velocity.x + m_velocity.z * m_velocity.z);
        m_bodySlamCharge01 = m_bodySlamRequestCharge01;
        m_bodySlamIsTap = !(m_bodySlamRequestCharge01 > 0.0f);
        m_bodySlamTravelled = 0.0f;
        m_bodySlamStallSteps = 0;

        if (m_bodySlamIsTap)
        {
            m_bodySlamDistanceTarget = m_tapSlamDistance;
            m_velocity = NS::Core::Vector3{dir.x * m_tapSlamSpeed, m_tapSlamUpSpeed, dir.z * m_tapSlamSpeed};
        }
        else
        {
            m_bodySlamDistanceTarget = m_bodySlamDistance;
            m_velocity = NS::Core::Vector3{dir.x * m_bodySlamSpeed, m_velocity.y, dir.z * m_bodySlamSpeed};
        }

        // 距離が 0 以下だと 1 歩目で終わって発動が消えるため、出さずに Locomotion のままにする
        if (!(m_bodySlamDistanceTarget > 0.0f))
            return false;

        m_machine.Change(*this, BodySlamState::k_Name);
        return true;
    }

    void CharacterMovementComponent::UpdateBodySlam(float dt) noexcept
    {
        const NS::Core::Vector3 before = RootTransform().Position();

        // 突進中に向きを変えられると当てる間合いを詰める意味が消えるので、水平は発動時の値で書き直す
        if (!m_bodySlamIsTap)
        {
            m_velocity.x = m_bodySlamDir.x * m_bodySlamSpeed;
            m_velocity.z = m_bodySlamDir.z * m_bodySlamSpeed;
        }

        const bool apex = std::abs(m_velocity.y) < m_apexHangVy;
        const float baseG = [&]() -> float {
            if (m_velocity.y > 0.0f)
                return m_gravityUp;
            return m_gravityDown;
        }();
        const float g = [&]() -> float {
            if (apex)
                return baseG * m_apexHangScale;
            return baseG;
        }();
        m_velocity.y += g * dt;

        NS::Physics::CapsuleMoverInput in{};
        in.position = before;
        in.velocity = m_velocity;
        in.dt = dt;
        in.capsuleRadius = m_capsuleRadius;
        in.capsuleHalfHeight = m_capsuleHalfHeight;
        in.physicsWorld = m_world;
        const NS::Physics::CapsuleMoverResult out = m_controller.Update(in);

        RootTransform().SetPosition(out.position);
        m_velocity = out.velocity;
        m_wasGrounded = m_isGrounded;
        m_isGrounded = out.grounded;

        if (!m_wasGrounded && m_isGrounded)
            m_jumpsRemaining = 1;
        if (m_isGrounded)
        {
            m_coyoteTimer = m_coyoteTime;
            m_lastGroundedPosition = out.position;
        }

        if (m_isGrounded)
            m_state = MovementState::Walking;
        else if (m_velocity.y > 0.0f)
            m_state = MovementState::Jumping;
        else
            m_state = MovementState::Falling;

        // 進んだ距離は実移動から測る。速度から積むと壁で止められた歩も進んだ扱いになる
        const float dx = out.position.x - before.x;
        const float dz = out.position.z - before.z;
        const float stepDistance = std::sqrt(dx * dx + dz * dz);
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
            m_machine.Change(*this, LocomotionState::k_Name);
        }
    }

    bool CharacterMovementComponent::TryGrabLedge(const NS::Core::Vector3& pos) noexcept
    {
        // 空中で下降中、 かつ前方入力がある時だけ掴む。 cooldown 中は無効
        if (m_ledgeRegrabCooldown > 0.0f || m_isGrounded || m_velocity.y > 0.0f)
            return false;
        if (m_desiredSpeedScale <= m_stickDeadzone)
            return false;

        NS::Core::Vector3 dir{m_desiredDir.x, 0.0f, m_desiredDir.z};
        const float dirLen = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        if (dirLen < 1e-4f)
            return false;
        dir.x /= dirLen;
        dir.z /= dirLen;

        // 手の高さ = capsule 上端。 そこから前方へ伸ばした probe 点が block の XZ 内に入り、
        // かつ block 上端が手の高さの帯に収まれば縁とみなす
        const float handY = pos.y + m_capsuleHalfHeight;
        const NS::Core::Vector3 probe{
            pos.x + dir.x * (m_capsuleRadius + k_LedgeReach),
            handY,
            pos.z + dir.z * (m_capsuleRadius + k_LedgeReach),
        };

        for (const NS::Core::AABB& box : WorldAabbs(m_world))
        {
            const float top = box.Center.y + box.Extents.y;
            if (top < handY - k_LedgeGrabBandLow || top > handY + k_LedgeGrabBandHigh)
                continue;
            if (probe.x < box.Center.x - box.Extents.x || probe.x > box.Center.x + box.Extents.x)
                continue;
            if (probe.z < box.Center.z - box.Extents.z || probe.z > box.Center.z + box.Extents.z)
                continue;

            // 接近軸の優勢成分で掴む手前面を決め、 その外側に capsule を寄せた hang 位置を出す
            NS::Core::Vector3 faceNormal{0.0f, 0.0f, 0.0f};
            NS::Core::Vector3 hang = pos;
            if (std::abs(dir.x) >= std::abs(dir.z))
            {
                const float sgn = [&]() -> float {
                    if (dir.x >= 0.0f)
                        return 1.0f;
                    return -1.0f;
                }();
                const float faceX = box.Center.x - sgn * box.Extents.x;
                faceNormal = NS::Core::Vector3{-sgn, 0.0f, 0.0f};
                hang.x = faceX - sgn * m_capsuleRadius;
                hang.z = NS::Core::Clamp(pos.z, box.Center.z - box.Extents.z, box.Center.z + box.Extents.z);
            }
            else
            {
                const float sgn = [&]() -> float {
                    if (dir.z >= 0.0f)
                        return 1.0f;
                    return -1.0f;
                }();
                const float faceZ = box.Center.z - sgn * box.Extents.z;
                faceNormal = NS::Core::Vector3{0.0f, 0.0f, -sgn};
                hang.z = faceZ - sgn * m_capsuleRadius;
                hang.x = NS::Core::Clamp(pos.x, box.Center.x - box.Extents.x, box.Center.x + box.Extents.x);
            }
            hang.y = top - m_capsuleHalfHeight;

            // 上面手前の mantle 先が別 block で塞がっているなら縁ではない。 掴まない
            const float mantleStep = 2.0f * m_capsuleRadius + k_LedgeMantleInset;
            const NS::Core::Vector3 mantleCheck{
                hang.x - faceNormal.x * mantleStep,
                top + m_capsuleHalfHeight,
                hang.z - faceNormal.z * mantleStep,
            };
            bool blocked = false;
            for (const NS::Core::AABB& other : WorldAabbs(m_world))
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
            m_velocity = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
            m_ledgeTopY = top;
            m_ledgeFaceNormal = faceNormal;
            m_ledgeHangTimer = 0.0f;
            m_state = MovementState::LedgeHanging;
            m_machine.Change(*this, LedgeHangState::k_Name);
            return true;
        }
        return false;
    }

    void CharacterMovementComponent::UpdateLedgeHang(float dt) noexcept
    {
        m_ledgeHangTimer += dt;
        NS::Core::Vector3 pos = RootTransform().Position();

        // 登る: jump は即時、 前入力での自動登りは最小ぶら下がり時間だけ一瞬ぶら下がりを見せてから
        // 面の内側へ押し込み block 上面に立たせて Walking へ
        const bool autoClimb = m_climbForward > k_LedgeInputThreshold && m_ledgeHangTimer >= k_LedgeMinHangTime;
        if (m_jumpPressedThisFrame || autoClimb)
        {
            const float mantleStep = 2.0f * m_capsuleRadius + k_LedgeMantleInset;
            m_ledgeMantleStart = pos;
            m_ledgeMantleEnd = NS::Core::Vector3{
                pos.x - m_ledgeFaceNormal.x * mantleStep,
                m_ledgeTopY + m_capsuleHalfHeight + m_capsuleRadius + k_LedgeMantleLift,
                pos.z - m_ledgeFaceNormal.z * mantleStep,
            };
            m_ledgeMantleTimer = 0.0f;
            m_state = MovementState::LedgeMantling;
            m_machine.Change(*this, LedgeMantleState::k_Name);
            m_velocity = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
            return;
        }

        // 落ちる: 後入力で手を放す。 面法線方向へ少し離して Falling、 即再掴みを cooldown で抑止
        if (m_climbForward < -k_LedgeInputThreshold)
        {
            pos.x += m_ledgeFaceNormal.x * k_LedgeDropOutward;
            pos.z += m_ledgeFaceNormal.z * k_LedgeDropOutward;
            RootTransform().SetPosition(pos);
            m_state = MovementState::Falling;
            m_machine.Change(*this, LocomotionState::k_Name);
            m_velocity = NS::Core::Vector3{
                m_ledgeFaceNormal.x * k_LedgeDropOutwardSpeed, 0.0f, m_ledgeFaceNormal.z * k_LedgeDropOutwardSpeed};
            m_isGrounded = false;
            m_ledgeRegrabCooldown = k_LedgeRegrabCooldownTime;
            return;
        }

        // それ以外: 縁にぶら下がったまま、 左右入力で縁に沿ってシミー移動する。 重力は無効
        pos.y = m_ledgeTopY - m_capsuleHalfHeight;
        if (std::abs(m_climbRight) > k_LedgeShimmyDeadzone)
        {
            // 面法線に水平直交する縁方向。 移動しても面からの距離は変わらない
            const NS::Core::Vector3 alongDir{-m_ledgeFaceNormal.z, 0.0f, m_ledgeFaceNormal.x};
            NS::Core::Vector3 shimmied = pos;
            shimmied.x += alongDir.x * m_climbRight * k_LedgeShimmySpeed * dt;
            shimmied.z += alongDir.z * m_climbRight * k_LedgeShimmySpeed * dt;
            // 移動先にも同じ高さの縁が続いている時だけ動く。 端なら止めて落とさない
            if (LedgeContinuesAt(shimmied))
                pos = shimmied;
        }
        RootTransform().SetPosition(pos);
        m_velocity = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
    }

    void CharacterMovementComponent::UpdateLedgeMantle(float dt) noexcept
    {
        m_ledgeMantleTimer += dt;
        const float t = NS::Core::Clamp(m_ledgeMantleTimer / k_LedgeMantleDuration, 0.0f, 1.0f);

        // 前半で縁の高さまで上昇、 後半で上面へ前進する 2 段モーション。 角への食い込みを避ける
        NS::Core::Vector3 pos;
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
        m_velocity = NS::Core::Vector3{0.0f, 0.0f, 0.0f};

        if (t >= 1.0f)
        {
            RootTransform().SetPosition(m_ledgeMantleEnd);
            m_state = MovementState::Walking;
            m_machine.Change(*this, LocomotionState::k_Name);
            m_isGrounded = true;
            m_wasGrounded = true;
            m_jumpsRemaining = 1;
            m_coyoteTimer = m_coyoteTime;
            m_ledgeRegrabCooldown = k_LedgeRegrabCooldownTime;
        }
    }

    bool CharacterMovementComponent::LedgeContinuesAt(const NS::Core::Vector3& hangPos) const noexcept
    {
        const NS::Core::Vector3 inward{-m_ledgeFaceNormal.x, 0.0f, -m_ledgeFaceNormal.z};
        const float handY = hangPos.y + m_capsuleHalfHeight;
        const NS::Core::Vector3 probe{
            hangPos.x + inward.x * (m_capsuleRadius + k_LedgeReach),
            handY,
            hangPos.z + inward.z * (m_capsuleRadius + k_LedgeReach),
        };

        for (const NS::Core::AABB& box : WorldAabbs(m_world))
        {
            const float top = box.Center.y + box.Extents.y;
            if (std::abs(top - m_ledgeTopY) > k_LedgeContinueTopTol)
                continue;
            if (probe.x < box.Center.x - box.Extents.x || probe.x > box.Center.x + box.Extents.x)
                continue;
            if (probe.z < box.Center.z - box.Extents.z || probe.z > box.Center.z + box.Extents.z)
                continue;

            // 乗り上がり先が別 block で塞がっていたら縁とみなさない。 オーバーハングの下では掴めない
            const float mantleStep = 2.0f * m_capsuleRadius + k_LedgeMantleInset;
            const NS::Core::Vector3 mantleCheck{
                hangPos.x - m_ledgeFaceNormal.x * mantleStep,
                m_ledgeTopY + m_capsuleHalfHeight,
                hangPos.z - m_ledgeFaceNormal.z * mantleStep,
            };
            bool blocked = false;
            for (const NS::Core::AABB& other : WorldAabbs(m_world))
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

    NS_CLASS(CharacterMovementComponent)
} // namespace NS::Object
