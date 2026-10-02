#include "Game/Player.h"

#include "Game/Level/CollisionInput.h"
#include "Game/Level/CourseDirector.h"
#include "Game/Level/Health.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/SlamArrow.h"
#include "Game/Level/TargetMarker.h"
#include "Game/Player/ChargeEffects.h"
#include "Game/Player/ImpactEffects.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerComponent.h"
#include "Game/Player/PlayerJudges.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/States/BodySlamPlayerState.h"
#include "Game/Player/States/BrakePlayerState.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/LedgeClimbingPlayerState.h"
#include "Game/Player/States/LedgeHangingPlayerState.h"
#include "Game/Player/States/ReboundPlayerState.h"
#include "Game/Player/States/WalkPlayerState.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Object/Components/Animation.h"
#include "Runtime/Object/Components/CapsuleCollider.h"
#include "Runtime/Object/Components/HitReaction.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Components/Model.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/Components/Shadow.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/IUse/IUseSceneObj.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Physics/PhysicsScene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

NS_CLASS(Player)

class Player::ChargeState final : public NS::Obj::StateOf<ChargeState, Player>
{
public:
    void OnEnter(Player& player) override { StartCoroutine(Run(player)); }
    void OnStep(Player&, float) override {}

private:
    NS::Core::Coroutine Run(Player& player)
    {
        while (true)
        {
            player.m_collisionInput->AdvanceCharge(player.m_chargeHeld, player.m_chargeDelta);
            if (!player.m_chargeHeld)
            {
                player.m_charge.Finish();
                co_return;
            }
            co_await NS::Core::NextFrame{};
        }
    }
};

Player::Player() noexcept
{
    m_charge.Finish();
    m_appearance = std::make_unique<NS::Game::Player::PlayerAppearance>();
    m_movement = std::make_unique<NS::Game::Player::PlayerComponent>();
    m_input = std::make_unique<NS::Obj::PlayerInput>();
    m_params = std::make_unique<NS::Game::Player::PlayerParams>();
    m_collisionInput = std::make_unique<NS::Game::Level::CollisionInput>();
    m_resolver = std::make_unique<NS::Game::Level::ImpactResolver>();
    m_targetMarker = std::make_unique<NS::Game::Level::TargetMarker>();
    m_slamArrow = std::make_unique<NS::Game::Level::SlamArrow>();
    m_chargeEffects = std::make_unique<NS::Game::Player::ChargeEffects>();
    m_impactEffects = std::make_unique<NS::Game::Player::ImpactEffects>();
    (void)CreatePart("Model");
    ModelPart()->SetMaterialRef("player");
    ModelPart()->SetBaseColor(NS::Core::Vector3{0.5f, 0.5f, 0.5f});
    AttachFixedComponent(*m_appearance);
    AttachFixedComponent(*m_movement);
    AttachFixedComponent(*m_input);
    AttachFixedComponent(*m_params);
    (void)CreatePart("Shadow");
    (void)CreatePart("Collider");
    (void)CreatePart("BodySensor");
    BodySensorPart()->SetType(NS::Obj::HitSensorType::PlayerBody);
    BodySensorPart()->SetCapsule(0.4f, 0.5f);
    AttachFixedComponent(*m_collisionInput);
    AttachFixedComponent(*m_resolver);
    (void)CreatePart("HitReaction");
    AttachFixedComponent(*m_targetMarker);
    AttachFixedComponent(*m_slamArrow);
    AttachFixedComponent(*m_chargeEffects);
    AttachFixedComponent(*m_impactEffects);
    m_movement->m_input = m_input.get();
    m_movement->m_params = m_params.get();
    m_collisionInput->m_player = this;
    m_collisionInput->m_params = m_params.get();
    // 部品を全部付けた後に組む。先頭の立ちの OnEnter が触る物が揃っている。並べた型が移れる状態の全部になる
    m_states = &BuildStateMachine<Player,
                                  NS::Game::Player::IdlePlayerState,
                                  NS::Game::Player::WalkPlayerState,
                                  NS::Game::Player::FallPlayerState,
                                  NS::Game::Player::LedgeHangingPlayerState,
                                  NS::Game::Player::LedgeClimbingPlayerState,
                                  NS::Game::Player::BodySlamPlayerState,
                                  NS::Game::Player::BrakePlayerState,
                                  NS::Game::Player::ReboundPlayerState>(*this);
    m_movement->m_states = m_states;
}

Player::~Player() = default;

void Player::ForEachPart(const PartVisitor& visitor) const
{
    NS::Obj::Actor::ForEachPart(visitor);
    visitor("Appearance", *m_appearance);
    visitor("Movement", *m_movement);
    visitor("Input", *m_input);
    visitor("Params", *m_params);
    visitor("ChargeControl", *m_collisionInput);
    visitor("ImpactResolver", *m_resolver);
    visitor("TargetMarker", *m_targetMarker);
    visitor("SlamArrow", *m_slamArrow);
    visitor("ChargeEffects", *m_chargeEffects);
    visitor("ImpactEffects", *m_impactEffects);
}

NS::Obj::CameraTargetState Player::GetCameraTargetState() const
{
    NS::Obj::CameraTargetState state{};
    if (const NS::Game::Player::PlayerComponent* movement = m_movement.get())
    {
        state.grounded = movement->IsGrounded();
        state.velocity = movement->Velocity();
        // 当たりの足元に立ち姿のカプセルを立てた時の中心を見る。玉の間は根が立ち姿の半長ぶん下がっているので、
        // 根を見ると押すたびに画面が 1 フレームで半長ぶん沈み、解けると跳ね上がる
        state.heightOffset = movement->StandingHalfHeight() - movement->CapsuleHalfHeight();
        state.hasRebound = true;
        state.rebound = NS::Obj::FollowReboundDesc{
            .rebounding = movement->IsRebounding(),
            .slamDirection = movement->BodySlamStartDirection(),
        };
    }
    if (const NS::Game::Level::CollisionInput* input = m_collisionInput.get())
    {
        // 溜め量は放した後も放した時の値を返し続けるので、押していないフレームは 0 を渡す
        const NS::Game::Level::ImpactInputJudge& judge = input->Judge();
        state.hasCharge = true;
        state.charge.held = judge.IsHeld();
        if (state.charge.held)
        {
            state.charge.charge01 = judge.Charge01();
        }
        NS::Game::Level::SlamLineTarget aim{};
        state.charge.hasAimTarget = input->TryGetAimTarget(aim);
        if (state.charge.hasAimTarget)
        {
            state.charge.aimTargetCenter =
                NS::Core::Vector3{aim.bounds.Center.x, aim.bounds.Center.y, aim.bounds.Center.z};
            state.charge.aimTargetRadius = std::max({aim.bounds.Extents.x, aim.bounds.Extents.y, aim.bounds.Extents.z});
        }
    }
    return state;
}

void Player::StepCharge(bool held, float dt)
{
    m_chargeHeld = held;
    m_chargeDelta = dt;
    if (m_charge.IsDead())
    {
        if (held)
        {
            m_charge.Build<ChargeState>(*this);
        }
        else
        {
            m_collisionInput->AdvanceCharge(false, dt);
        }
        return;
    }
    m_charge.Step(*this, dt);
}

void Player::UpdateAnimation()
{
    NS::Obj::Animation* animation = AnimationPart();
    if (animation == nullptr)
    {
        return;
    }

    const float lateralSpeed = m_movement->LateralVelocity().Length();
    const std::string_view clip = ChooseClip(lateralSpeed);

    if (clip != m_appliedClip)
    {
        if (animation->SelectClip(clip))
        {
            m_appliedClip = clip;
        }
        else if (animation->SelectClip(m_params->m_idleClip))
        {
            m_appliedClip = m_params->m_idleClip;
        }
    }

    animation->SetSpeed(ChoosePlaybackSpeed(m_appliedClip, lateralSpeed));
}

void Player::ReadInput()
{
    TickPart(m_input.get());
}

void Player::Update()
{
    Update(m_collisionInput->ReadHeld());
}

void Player::Update(bool chargeHeld)
{
    const float dt = NS::Platform::FrameTimer::FixedDelta();
    TickPart(ModelPart());
    if (m_collisionInput->IsActive())
    {
        m_collisionInput->Observe(chargeHeld);
    }
    if (m_resolver->IsActive())
    {
        NS::Core::Vector3 predicted = m_movement->BodySlamVelocity();
        if (m_collisionInput->IsActive())
        {
            predicted = m_collisionInput->PredictedSlamVelocity();
        }
        m_resolver->ObserveImpact(predicted);
    }
    if (m_collisionInput->IsActive())
    {
        m_collisionInput->AdvanceState(dt);
    }
    if (m_resolver->IsActive())
    {
        m_resolver->StepState();
    }
    if (m_movement->IsActive() && dt > 0.0f)
    {
        m_movement->PrepareStateStep();
        StepStateMachine();
        m_movement->FinishStateStep(dt);
    }
    else
    {
        m_movement->OnStepSkipped();
    }
    if (m_collisionInput->IsActive())
    {
        m_collisionInput->ApplyControl();
    }
    TickPart(m_movement.get());
    UpdateAnimation();
    TickPart(m_targetMarker.get());
    TickPart(m_slamArrow.get());
    TickPart(HitReactionPart());
    TickPart(m_appearance.get());
    TickPart(m_chargeEffects.get());
    TickPart(m_impactEffects.get());
}

std::string_view Player::ChooseClip(float lateralSpeed) const noexcept
{
    if (m_states->IsCurrent<NS::Game::Player::LedgeHangingPlayerState>())
    {
        if (!m_params->m_ledgeHangClip.empty())
        {
            return m_params->m_ledgeHangClip;
        }
        return m_params->m_idleClip;
    }

    if (!m_movement->IsGrounded())
    {
        if (m_movement->VerticalVelocity() > 0.0f && !m_params->m_jumpClip.empty())
        {
            return m_params->m_jumpClip;
        }
        if (m_movement->VerticalVelocity() <= 0.0f && !m_params->m_fallClip.empty())
        {
            return m_params->m_fallClip;
        }
        return m_params->m_idleClip;
    }

    if (lateralSpeed <= NS::Core::k_Epsilon)
    {
        return m_params->m_idleClip;
    }

    const float maxSpeed = m_movement->MaxSpeed();
    if (maxSpeed > 0.0f && lateralSpeed >= maxSpeed * m_params->m_runBlendRatio && !m_params->m_runClip.empty())
    {
        return m_params->m_runClip;
    }
    if (!m_params->m_walkClip.empty())
    {
        return m_params->m_walkClip;
    }
    return m_params->m_idleClip;
}

float Player::ChoosePlaybackSpeed(std::string_view clip, float lateralSpeed) const noexcept
{
    if (clip != m_params->m_walkClip && clip != m_params->m_runClip)
    {
        return 1.0f;
    }

    const float maxSpeed = m_movement->MaxSpeed();
    if (maxSpeed <= 0.0f)
    {
        return 1.0f;
    }
    return std::max(m_params->m_minPlaybackSpeed, lateralSpeed / maxSpeed);
}

void Player::OnEndPlay()
{
    m_charge.Finish();
    m_states->Reset();
    m_appliedClip.clear();
    NS::Obj::Actor::OnEndPlay();
}

void Player::InitAfterPlacement()
{
    ResetHealth();
    // 死とゴールを伝える先。先に作っておくと、最初の知らせの段で流れが進む
    (void)NS::Obj::GetOrCreateSceneObj<NS::Game::Level::CourseDirector>(*this);
}

bool Player::ReceiveMsg(const NS::Obj::Message& msg, NS::Obj::HitSensor* sender, NS::Obj::HitSensor* receiver)
{
    (void)sender;
    (void)receiver;
    if (NS::Game::Level::IsMsgKill(msg))
    {
        Die();
        if (NS::Game::Level::CourseDirector* director =
                NS::Obj::GetOrCreateSceneObj<NS::Game::Level::CourseDirector>(*this))
        {
            director->NotifyPlayerDead();
        }
        return true;
    }
    if (NS::Game::Level::IsMsgGoal(msg))
    {
        if (NS::Game::Level::CourseDirector* director =
                NS::Obj::GetOrCreateSceneObj<NS::Game::Level::CourseDirector>(*this))
        {
            director->NotifyGoal();
        }
        return true;
    }
    if (const NS::Game::Level::MsgCourseRestart* restart = NS::Obj::MsgCast<NS::Game::Level::MsgCourseRestart>(msg))
    {
        RestartFrom(restart->Baseline());
        return true;
    }
    if (const NS::Game::Level::MsgInputLock* lock = NS::Obj::MsgCast<NS::Game::Level::MsgInputLock>(msg))
    {
        if (NS::Obj::PlayerInput* input = m_input.get())
        {
            input->SetActive(!lock->Locked());
        }
        return true;
    }
    return false;
}

void Player::RestartFrom(const nlohmann::json& baseline) noexcept
{
    m_charge.Finish();
    // 出現位置はエディタで配置したプレイヤーの capsule 中心の world 位置そのもの
    // 凍結に既にある値なので写しは持たず、その都度読む。居なければ新規レベルで置く位置へ戻す
    NS::Core::Vector3 spawn{0.0f, 1.41f, 0.0f};
    const std::size_t index = NS::Obj::FindObjectIndexById(baseline, Id());
    if (index != NS::Obj::k_NoObjectIndex)
    {
        spawn = NS::Obj::ObjectPosition(NS::Obj::SceneJsonObjects(baseline)[index]);
    }

    Root().SetPosition(spawn);
    if (NS::Game::Player::PlayerComponent* movement = m_movement.get())
    {
        movement->ResetState();
    }
    ResetHealth();
    Appear();
}

void Player::ApplyDamage(int amount) noexcept
{
    m_health.ApplyDamage(amount);
}

void Player::Die() noexcept
{
    m_charge.Finish();
    m_health.Kill();
    Kill();
}

void Player::ResetHealth() noexcept
{
    if (const NS::Game::Player::PlayerParams* params = m_params.get())
    {
        m_health.SetMaxHealth(params->MaxHealth());
    }
    m_health.Reset();
}

bool Player::IsDead() const noexcept
{
    return m_health.IsDead();
}

int Player::Health() const noexcept
{
    return m_health.Current();
}

// ---- 移動の組み立て ----
// 状態の OnStep がこの順で呼ぶ。身体 (PlayerComponent) の速度・接地・重力の計算を呼ぶだけで、値は PlayerParams から読む

void Player::TickTimers(float dt) noexcept
{
    NS::Game::Player::PlayerComponent& body = *m_movement;
    body.m_bufferTimer -= dt;
    if (m_input->JumpPressed())
    {
        body.m_bufferTimer = m_params->m_jumpBufferTime;
    }

    const bool inAir = !body.IsGrounded();
    if (inAir)
    {
        body.m_coyoteTimer -= dt;
    }
}

void Player::AccelerateToInputDirection(float dt) noexcept
{
    NS::Game::Player::PlayerComponent& body = *m_movement;
    NS::Core::Vector3 direction{};
    if (!body.HasMoveInput() || !NS::Core::TryNormalizeHorizontal(body.DesiredDirection(), direction))
    {
        return;
    }

    const float topSpeed = std::max(body.MaxSpeed() * body.DesiredSpeedScale(), m_params->m_walkSpeed);
    float acceleration = m_params->m_airAcceleration;
    if (body.IsGrounded())
    {
        acceleration = m_params->m_acceleration;
    }
    body.Accelerate(direction, m_params->m_turningDrag, acceleration, topSpeed, dt);
}

void Player::ApplyFriction(float dt) noexcept
{
    m_movement->Decelerate(m_params->m_friction, dt);
}

void Player::ApplyBrake(float dt) noexcept
{
    m_movement->Decelerate(m_params->m_deceleration, dt);
}

void Player::Jump(float) noexcept
{
    NS::Game::Player::PlayerComponent& body = *m_movement;
    if (NS::Game::Player::PlayerJudgeJump::Judge(
            body.IsGrounded(), body.m_coyoteTimer, body.m_jumpsRemaining, m_input->JumpPressed(), body.m_bufferTimer))
    {
        body.SetVerticalVelocity(m_params->m_jumpImpulse);
        --body.m_jumpsRemaining;
        body.m_bufferTimer = 0.0f;
        body.m_coyoteTimer = 0.0f;
        body.m_playerEvents.onJump.Invoke();
    }
}

void Player::CutJumpRelease() noexcept
{
    NS::Game::Player::PlayerComponent& body = *m_movement;
    if (body.m_prevJumpHeld && !m_input->JumpHeld() && body.VerticalVelocity() > 0.0f)
    {
        body.SetVerticalVelocity(body.VerticalVelocity() * m_params->m_jumpReleaseScale);
    }
}

void Player::Gravity(float dt) noexcept
{
    NS::Game::Player::PlayerComponent& body = *m_movement;
    const bool apex = std::abs(body.VerticalVelocity()) < m_params->m_apexHangVy;

    float baseG = m_params->m_gravityDown;
    if (body.VerticalVelocity() > 0.0f)
    {
        baseG = m_params->m_gravityUp;
    }

    float g = baseG;
    if (apex)
    {
        g = baseG * m_params->m_apexHangScale;
    }

    body.Gravity(g, dt);
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
    m_movement->Gravity(g, dt);
}

void Player::ReboundGravity(float dt) noexcept
{
    NS::Game::Player::PlayerComponent& body = *m_movement;
    if (!(body.VerticalVelocity() > 0.0f))
    {
        Gravity(dt);
        return;
    }

    float g = m_params->m_gravityUp * m_params->m_reboundRiseGravityScale;
    if (std::abs(body.VerticalVelocity()) < m_params->m_apexHangVy)
    {
        g = g * m_params->m_apexHangScale;
    }
    body.Gravity(g, dt);
}

void Player::AccelerateDuringRebound(float dt) noexcept
{
    NS::Game::Player::PlayerComponent& body = *m_movement;
    NS::Core::Vector3 direction{};
    if (!body.HasMoveInput() || !NS::Core::TryNormalizeHorizontal(body.DesiredDirection(), direction))
    {
        return;
    }

    const float topSpeed = std::max(body.MaxSpeed() * body.DesiredSpeedScale(), m_params->m_walkSpeed);
    // 入力の向きからずれた速度は削らない。削ると横へ倒しただけで相手から離れる流れが消え、
    // 弾かれる向きが当て方でなくスティックで決まる。触って詰める値ではないので欄にしない
    const float turningDrag = 0.0f;
    // 明けのフレームは止める前の接地の印が残っている。接地を見て地上の加速度を選ぶと、そのフレームだけ大きく曲がる
    body.Accelerate(direction, turningDrag, m_params->m_reboundAirAcceleration, topSpeed, dt);
}

void Player::UpdateBodySlam(float dt) noexcept
{
    NS::Game::Player::PlayerComponent& body = *m_movement;
    // 突進中に向きを変えられると当てる間合いを詰める意味が消えるので、水平は発動時の値で書き直す
    if (!body.m_bodySlamIsTap)
    {
        body.SetLateralVelocity(NS::Core::Vector3{
            body.m_bodySlamDir.x * m_params->m_bodySlamSpeed, 0.0f, body.m_bodySlamDir.z * m_params->m_bodySlamSpeed});
    }

    if (body.m_bodySlamIsTap)
    {
        TapSlamGravity(dt);
    }
    else
    {
        Gravity(dt);
    }
}

// ---- 崖つかまり ----
// 縁の検出・つかまり・登り。つかまりの位置と向きの記録は PlayerComponent に置いたまま、friend 経由で読み書きする

namespace
{
    // 掴まりの走査で見る AABB 群。physics 未設定なら空を返し、掴めないだけにする
    [[nodiscard]] std::vector<NS::Core::AABB> BoxesTouchingBand(const NS::Phys::PhysicsScene* physics,
                                                                const NS::Core::Vector3& probe,
                                                                float below,
                                                                float above)
    {
        if (physics == nullptr)
        {
            return {};
        }

        NS::Core::AABB region;
        region.Center = NS::Core::Vector3{probe.x, probe.y + 0.5f * (above - below), probe.z};
        region.Extents = NS::Core::Vector3{0.0f, 0.5f * (above + below), 0.0f};
        return physics->OverlapBox(region);
    }

    [[nodiscard]] std::vector<NS::Core::AABB> BoxesAtPoint(const NS::Phys::PhysicsScene* physics,
                                                           const NS::Core::Vector3& point)
    {
        if (physics == nullptr)
        {
            return {};
        }

        NS::Core::AABB region;
        region.Center = point;
        region.Extents = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        return physics->OverlapBox(region);
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
    NS::Game::Player::PlayerComponent& body = *m_movement;
    if (!NS::Game::Player::PlayerJudgeLedgeGrab::Judge(
            body.IsGrounded(), body.VerticalVelocity(), body.m_wasBodySlamming))
    {
        return false;
    }

    NS::Core::Vector3 dir{};
    if (!NS::Core::TryNormalizeHorizontal(body.m_facingDir, dir))
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
    const float above = body.m_lastMoveDistance;
    for (const NS::Core::AABB& box :
         BoxesTouchingBand(body.ScenePhysics(), probe, m_params->m_ledgeGrabBelowHand, above))
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
        for (const NS::Core::AABB& other : BoxesAtPoint(body.ScenePhysics(), mantleCheck))
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
        body.ChangeCurled(false);
        Root().SetPosition(hang);
        body.SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
        body.m_ledgeTopY = top;
        body.m_ledgeFaceNormal = faceNormal;
        (void)m_states->Change<NS::Game::Player::LedgeHangingPlayerState>(*this);
        body.m_playerEvents.onLedgeGrabbed.Invoke();
        return true;
    }
    return false;
}

bool Player::HoldLedge() noexcept
{
    NS::Game::Player::PlayerComponent& body = *m_movement;
    float top = 0.0f;
    if (!FindLedgeTopAt(Root().Position(), top))
    {
        DropLedge();
        return false;
    }

    body.m_ledgeTopY = top;
    NS::Core::Vector3 pos = Root().Position();
    pos.y = body.m_ledgeTopY - body.CapsuleHalfHeight();
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

    NS::Game::Player::PlayerComponent& body = *m_movement;
    body.SetVerticalVelocity(m_params->m_jumpImpulse);
    body.SetGrounded(false);
    (void)m_states->Change<NS::Game::Player::FallPlayerState>(*this);
    body.m_playerEvents.onJump.Invoke();
    return true;
}

void Player::ClimbLedge() noexcept
{
    NS::Game::Player::PlayerComponent& body = *m_movement;
    const NS::Core::Vector3 pos = Root().Position();
    // ぶら下がりの中心は面から半径ぶん外。直径ぶん奥へ進めると中心が縁から半径ぶん内側に入り、体が上面に乗る
    const float mantleStep = 2.0f * body.CapsuleRadius();
    body.m_ledgeMantleStart = pos;
    body.m_ledgeMantleEnd = NS::Core::Vector3{
        pos.x - body.m_ledgeFaceNormal.x * mantleStep,
        body.m_ledgeTopY + body.CapsuleHalfHeight() + body.CapsuleRadius(),
        pos.z - body.m_ledgeFaceNormal.z * mantleStep,
    };
    body.m_ledgeMantleTimer = 0.0f;
    (void)m_states->Change<NS::Game::Player::LedgeClimbingPlayerState>(*this);
    body.SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
    body.m_playerEvents.onLedgeClimbing.Invoke();
}

void Player::DropLedge() noexcept
{
    NS::Game::Player::PlayerComponent& body = *m_movement;
    (void)m_states->Change<NS::Game::Player::FallPlayerState>(*this);
    body.SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
    body.SetGrounded(false);
    // 壁と逆を向いて落ちる。壁を向いたままだと、帯の上の余白に縁が入って次のフレームで掴み直す
    body.m_facingDir = body.m_ledgeFaceNormal;
}

void Player::Shimmy(float dt) noexcept
{
    NS::Game::Player::PlayerComponent& body = *m_movement;
    if (body.ClimbRight() != 0.0f)
    {
        // 面法線に水平直交する縁方向。動いても面からの距離は変わらない
        const NS::Core::Vector3 pos = Root().Position();
        const NS::Core::Vector3 alongDir{-body.m_ledgeFaceNormal.z, 0.0f, body.m_ledgeFaceNormal.x};
        NS::Core::Vector3 shimmied = pos;
        shimmied.x += alongDir.x * body.ClimbRight() * m_params->m_ledgeShimmySpeed * dt;
        shimmied.z += alongDir.z * body.ClimbRight() * m_params->m_ledgeShimmySpeed * dt;
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
    NS::Game::Player::PlayerComponent& body = *m_movement;
    body.m_ledgeMantleTimer += dt;
    float t = 1.0f;
    if (m_params->m_ledgeClimbDuration > 0.0f)
    {
        t = NS::Core::Clamp(body.m_ledgeMantleTimer / m_params->m_ledgeClimbDuration, 0.0f, 1.0f);
    }

    // 2 段に割るのは角への食い込みを避けるため。前半は上昇だけで前へ進まない
    NS::Core::Vector3 pos{0.0f, 0.0f, 0.0f};
    if (t < 0.5f)
    {
        const float u = t / 0.5f;
        pos.x = body.m_ledgeMantleStart.x;
        pos.z = body.m_ledgeMantleStart.z;
        pos.y = body.m_ledgeMantleStart.y + (body.m_ledgeMantleEnd.y - body.m_ledgeMantleStart.y) * u;
    }
    else
    {
        const float u = (t - 0.5f) / 0.5f;
        pos.x = body.m_ledgeMantleStart.x + (body.m_ledgeMantleEnd.x - body.m_ledgeMantleStart.x) * u;
        pos.z = body.m_ledgeMantleStart.z + (body.m_ledgeMantleEnd.z - body.m_ledgeMantleStart.z) * u;
        pos.y = body.m_ledgeMantleEnd.y;
    }
    Root().SetPosition(pos);
    body.SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});

    if (t >= 1.0f)
    {
        Root().SetPosition(body.m_ledgeMantleEnd);
        (void)m_states->Change<NS::Game::Player::IdlePlayerState>(*this);
        body.SetGrounded(true);
        body.m_jumpsRemaining = 1;
        body.m_coyoteTimer = m_params->m_coyoteTime;
    }
}

bool Player::FindLedgeTopAt(const NS::Core::Vector3& hangPos, float& outTop) const noexcept
{
    const NS::Game::Player::PlayerComponent& body = *m_movement;
    const NS::Core::Vector3 inward{-body.m_ledgeFaceNormal.x, 0.0f, -body.m_ledgeFaceNormal.z};
    const float handY = hangPos.y + body.CapsuleHalfHeight();
    const NS::Core::Vector3 probe{
        hangPos.x + inward.x * (body.CapsuleRadius() + m_params->m_ledgeReach),
        handY,
        hangPos.z + inward.z * (body.CapsuleRadius() + m_params->m_ledgeReach),
    };

    for (const NS::Core::AABB& box :
         BoxesTouchingBand(body.ScenePhysics(), probe, m_params->m_ledgeGrabBelowHand, 0.0f))
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
            hangPos.x - body.m_ledgeFaceNormal.x * mantleStep,
            top + body.CapsuleHalfHeight(),
            hangPos.z - body.m_ledgeFaceNormal.z * mantleStep,
        };
        bool blocked = false;
        for (const NS::Core::AABB& other : BoxesAtPoint(body.ScenePhysics(), mantleCheck))
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

Player* FindPlayer(NS::Obj::ObjectList& objects) noexcept
{
    for (NS::Obj::Actor* obj : objects)
    {
        if (std::strcmp(obj->ClassName(), "Player") == 0)
        {
            return static_cast<Player*>(obj);
        }
    }
    return nullptr;
}

bool IsPlayerObject(const nlohmann::json& object) noexcept
{
    return NS::Obj::ObjectJsonClass(object) == "Player";
}

std::size_t FindPlayerObjectIndex(const nlohmann::json& scene) noexcept
{
    const nlohmann::json& objects = NS::Obj::SceneJsonObjects(scene);
    for (std::size_t i = 0; i < objects.size(); ++i)
    {
        if (IsPlayerObject(objects[i]))
        {
            return i;
        }
    }
    return NS::Obj::k_NoObjectIndex;
}

nlohmann::json MakePlayerObject(const NS::Core::Vector3& position, const NS::Core::Quaternion& rotation)
{
    // 構成は Player のコンストラクタが決める。ひな形は型名だけ持ち、値はコード既定を使う
    nlohmann::json object = NS::Obj::MakePrototypeJson<Player>();
    NS::Obj::SetObjectPosition(object, position);
    NS::Obj::SetObjectRotation(object, rotation);
    // 根のスケールは既定の 1 のまま。1 でないと玉が楕円に伸び、差し替えたモデルも同じ比で伸びる
    return object;
}

std::uint32_t PlayerObjectId(const nlohmann::json& scene) noexcept
{
    const std::size_t index = FindPlayerObjectIndex(scene);
    if (index == NS::Obj::k_NoObjectIndex)
    {
        return NS::Obj::k_NoObjectId;
    }
    return NS::Obj::ObjectJsonId(NS::Obj::SceneJsonObjects(scene)[index]);
}

bool EnsurePlayerObject(nlohmann::json& scene)
{
    bool created = false;
    if (FindPlayerObjectIndex(scene) == NS::Obj::k_NoObjectIndex)
    {
        // capsule 中心の高さは、床 block 上面 0.5 + capsule 半高 0.9 + 1cm
        NS::Obj::SceneJsonObjects(scene).push_back(
            MakePlayerObject(NS::Core::Vector3{0.0f, 1.41f, 0.0f}, NS::Core::Quaternion{}));
        created = true;
    }

    std::size_t count = 0;
    for (const nlohmann::json& object : NS::Obj::SceneJsonObjects(scene))
    {
        if (IsPlayerObject(object))
        {
            ++count;
        }
    }
    if (count > 1)
    {
        NS_LOG_WARN(Game, "プレイヤーが {} 体ある。先頭の 1 体を正とし、残りは無効として扱う", count);
    }

    // 追従カメラが Target へ書き込む id が要るので、ここで採番まで済ませる
    NS::Obj::EnsureUniqueObjectIds(scene);
    return created;
}
