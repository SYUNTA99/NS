#include "Game/Player.h"

#include "Game/Entity/EntityComponent.h"
#include "Game/Level/CollisionInput.h"
#include "Game/Level/CourseDirector.h"
#include "Game/Level/Health.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/SlamArrow.h"
#include "Game/Level/TargetMarker.h"
#include "Game/Player/ChargeEffects.h"
#include "Game/Player/HorizontalTurn.h"
#include "Game/Player/ImpactEffects.h"
#include "Game/Player/PlayerAppearance.h"
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
    m_body = std::make_unique<NS::Game::Entity::EntityComponent>();
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
    AttachFixedComponent(*m_body);
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
}

Player::~Player() = default;

void Player::ForEachPart(const PartVisitor& visitor) const
{
    NS::Obj::Actor::ForEachPart(visitor);
    visitor("Appearance", *m_appearance);
    visitor("Movement", *m_body);
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
    state.grounded = m_body->IsGrounded();
    state.velocity = m_body->Velocity();
    // 当たりの足元に立ち姿のカプセルを立てた時の中心を見る。玉の間は根が立ち姿の半長ぶん下がっているので、
    // 根を見ると押すたびに画面が 1 フレームで半長ぶん沈み、解けると跳ね上がる
    state.heightOffset = m_body->StandingHalfHeight() - m_body->CapsuleHalfHeight();
    state.hasRebound = true;
    state.rebound = NS::Obj::FollowReboundDesc{
        .rebounding = IsRebounding(),
        .slamDirection = BodySlamStartDirection(),
    };
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

    const float lateralSpeed = m_body->LateralVelocity().Length();
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
        NS::Core::Vector3 predicted = BodySlamVelocity();
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
    if (m_body->IsActive() && dt > 0.0f)
    {
        PrepareStateStep();
        StepStateMachine();
        FinishStateStep(dt);
    }
    else
    {
        SkipBodyStep();
    }
    if (m_collisionInput->IsActive())
    {
        m_collisionInput->ApplyControl();
    }
    if (m_body->IsActive())
    {
        if (dt > 0.0f)
        {
            MoveBody(dt);
        }
        else
        {
            SkipBodyStep();
        }
    }
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

    if (!m_body->IsGrounded())
    {
        if (m_body->VerticalVelocity() > 0.0f && !m_params->m_jumpClip.empty())
        {
            return m_params->m_jumpClip;
        }
        if (m_body->VerticalVelocity() <= 0.0f && !m_params->m_fallClip.empty())
        {
            return m_params->m_fallClip;
        }
        return m_params->m_idleClip;
    }

    if (lateralSpeed <= NS::Core::k_Epsilon)
    {
        return m_params->m_idleClip;
    }

    const float maxSpeed = MaxSpeed();
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

    const float maxSpeed = MaxSpeed();
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
    ResetState();
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
// 状態の OnStep がこの順で呼ぶ。身体 (EntityComponent) の速度・接地・重力の計算を呼ぶだけで、値は PlayerParams から読む

void Player::SetDesiredMove(const NS::Core::Vector3& worldDir, float speedScale01) noexcept
{
    m_input->SetDesiredMove(worldDir, speedScale01);
}

float Player::DesiredSpeedScale() const noexcept
{
    return m_input->DesiredSpeedScale();
}

NS::Core::Vector3 Player::DesiredDirection() const noexcept
{
    return m_input->DesiredDirection();
}

void Player::SetClimbMove(float localRight, float localForward) noexcept
{
    m_input->SetClimbMove(localRight, localForward);
}

float Player::ClimbRight() const noexcept
{
    return m_input->ClimbRight();
}

float Player::ClimbForward() const noexcept
{
    return m_input->ClimbForward();
}

void Player::SetJumpPressed() noexcept
{
    m_input->SetJumpPressed();
}

void Player::SetReleaseLedgePressed() noexcept
{
    m_input->SetReleaseLedgePressed();
}

void Player::SetJumpHeld(bool held) noexcept
{
    m_input->SetJumpHeld(held);
}

float Player::MaxSpeed() const noexcept
{
    const float capped = m_params->m_runSpeed * m_maxSpeedScale;
    if (capped < 0.0f)
    {
        return 0.0f;
    }
    return capped;
}

void Player::SetMaxSpeedScale(float scale) noexcept
{
    // 非数を入れると MaxSpeed() との比較が偽になり、突進明けに水平の速さが切られない
    if (!std::isfinite(scale))
    {
        return;
    }
    m_maxSpeedScale = scale;
}

float Player::RunSpeed() const noexcept
{
    return m_params->m_runSpeed;
}

bool Player::IsBodySlamming() const noexcept
{
    // 状態機械を組んでいる最中 (先頭の OnEnter) はまだ預かっていないので nullptr を見る
    return m_states != nullptr && m_states->IsCurrent<NS::Game::Player::BodySlamPlayerState>();
}

float Player::BodySlamProgress01() const noexcept
{
    if (!IsBodySlamming() || !(m_slam.distanceTarget > 0.0f))
    {
        return 0.0f;
    }
    return NS::Core::Clamp(m_slam.travelled / m_slam.distanceTarget, 0.0f, 1.0f);
}

float Player::BodySlamDistance() const noexcept
{
    return m_params->m_bodySlamDistance;
}

bool Player::IsRebounding() const noexcept
{
    return m_states != nullptr && m_states->IsCurrent<NS::Game::Player::ReboundPlayerState>();
}

void Player::ResetState() noexcept
{
    m_body->SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
    m_input->ResetMovementInput();
    m_prevJumpHeld = false;
    m_jumpsRemaining = 1;
    m_coyoteTimer = 0.0f;
    m_bufferTimer = 0.0f;
    m_body->SetGrounded(false);
    m_ledgeTopY = 0.0f;
    m_ledgeFaceNormal = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
    m_ledgeMantleTimer = 0.0f;
    m_facingDir = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
    m_lastMoveDistance = 0.0f;
    m_request.bufferRemaining = 0.0f;
    m_request.spent = false;
    m_slam.isTap = false;
    m_request.charge01 = 0.0f;
    m_request.dir = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
    m_request.hasDir = false;
    m_slam.charge01 = 0.0f;
    m_slam.travelled = 0.0f;
    m_slam.distanceTarget = 0.0f;
    m_slam.justStarted = false;
    m_slam.dir = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
    m_slam.startDir = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
    m_rebound.direction = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
    ForgetHoming();
    // 当たりの形だけを立ち姿へ戻し、根は動かさない。出直しは根を出現位置へ置いてから呼ぶので、
    // 丸まりを解く時のように根を上げると出現位置より半長ぶん高く湧いた
    m_curled = false;
    m_body->SetSphereShape(false);
    m_bodySlamHeld = false;
    m_slam.wasSlamming = false;
    m_states->Reset();
}

void Player::SkipBodyStep() noexcept
{
    m_input->ConsumePressed();
    m_prevJumpHeld = m_input->JumpHeld();
}

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
            m_facingDir = NS::Game::Player::TurnHorizontalToward(current, target, maxTurn);
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
    NS::Game::Entity::EntityComponent& body = *m_body;
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
    NS::Game::Entity::EntityComponent& body = *m_body;
    NS::Core::Vector3 direction{};
    if (!NS::Game::Player::PlayerJudgeMoveInput::Judge(DesiredSpeedScale(), m_params->m_stickDeadzone) ||
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
    NS::Game::Entity::EntityComponent& body = *m_body;
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
    NS::Game::Entity::EntityComponent& body = *m_body;
    if (m_prevJumpHeld && !m_input->JumpHeld() && body.VerticalVelocity() > 0.0f)
    {
        body.SetVerticalVelocity(body.VerticalVelocity() * m_params->m_jumpReleaseScale);
    }
}

void Player::Gravity(float dt) noexcept
{
    NS::Game::Entity::EntityComponent& body = *m_body;
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
    m_body->Gravity(g, dt);
}

void Player::ReboundGravity(float dt) noexcept
{
    NS::Game::Entity::EntityComponent& body = *m_body;
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
    NS::Game::Entity::EntityComponent& body = *m_body;
    NS::Core::Vector3 direction{};
    if (!NS::Game::Player::PlayerJudgeMoveInput::Judge(DesiredSpeedScale(), m_params->m_stickDeadzone) ||
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
    NS::Game::Entity::EntityComponent& body = *m_body;
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

// ---- 突進と反発 ----

void Player::RequestBodySlam(float charge01) noexcept
{
    // そのフレームで出せないと押しが無言で消える。ジャンプと同じ先行入力時間だけ覚える
    m_request.bufferRemaining = m_params->m_jumpBufferTime;
    // 非数は 0..1 への丸めを素通りして溜め量に残るため、入口で 0 へ倒す
    if (!std::isfinite(charge01))
    {
        m_request.charge01 = 0.0f;
    }
    else
    {
        m_request.charge01 = NS::Core::Clamp(charge01, 0.0f, 1.0f);
    }
    // 残すと、先行入力のうちに来たタップが前の溜めた突進の向きへ出る
    m_request.hasDir = false;
}

void Player::RequestBodySlam(float charge01, const NS::Core::Vector3& aimDirection) noexcept
{
    RequestBodySlam(charge01);
    NS::Core::Vector3 dir{};
    if (!NS::Core::TryNormalizeHorizontal(aimDirection, dir))
    {
        return;
    }
    // 非数と無限の向きは正規化を通り抜ける
    if (!std::isfinite(dir.x) || !std::isfinite(dir.z))
    {
        return;
    }
    m_request.dir = dir;
    m_request.hasDir = true;
}

NS::Core::Vector3 Player::BodySlamVelocity() const noexcept
{
    const NS::Game::Entity::EntityComponent& body = *m_body;
    if (!IsBodySlamming())
    {
        return body.Velocity();
    }

    float speed = m_params->m_bodySlamSpeed;
    if (m_slam.isTap)
    {
        speed = m_params->m_tapSlamSpeed;
    }
    return NS::Core::Vector3{m_slam.dir.x * speed, body.VerticalVelocity(), m_slam.dir.z * speed};
}

void Player::CancelBodySlam() noexcept
{
    if (!IsBodySlamming())
    {
        return;
    }
    EndBodySlam();
}

void Player::EndBodySlam() noexcept
{
    NS::Game::Entity::EntityComponent& body = *m_body;
    m_slam.travelled = 0.0f;
    m_slam.distanceTarget = 0.0f;
    // 残すと、次の溜めが前の突進で寄せた分を累計に引き継ぎ、放す時に溜めていない分まで回る
    ForgetHoming();

    // 加速は最高速を超えた速さを削らない。切らないと、倒している間は突進の速さのまま走り続ける
    const NS::Core::Vector3 lateral = body.LateralVelocity();
    const float speed = std::sqrt(lateral.x * lateral.x + lateral.z * lateral.z);
    const float cap = MaxSpeed();
    if (speed > cap)
    {
        const float scale = cap / speed;
        body.SetLateralVelocity(NS::Core::Vector3{lateral.x * scale, 0.0f, lateral.z * scale});
    }

    if (body.IsGrounded())
    {
        (void)m_states->Change<NS::Game::Player::WalkPlayerState>(*this);
    }
    else
    {
        (void)m_states->Change<NS::Game::Player::FallPlayerState>(*this);
    }
    m_playerEvents.onBodySlamEnded.Invoke();
}

void Player::AdvanceBodySlamTravel(const NS::Core::Vector3& delta) noexcept
{
    if (!IsBodySlamming())
    {
        return;
    }

    // 進んだ距離は実際に動いた量から測る。突進の速さから積むと壁で止められたフレームも進んだ扱いになる
    const float stepDistance = std::sqrt(delta.x * delta.x + delta.z * delta.z);
    m_slam.travelled += stepDistance;

    // 進めないフレームで打ち切る。壁で止められると進んだ距離が伸びず、突進から出られなくなる
    // 発動したフレームは見ない。ここで打ち切ると発動から打ち切りまでに ImpactResolver が
    // 一度も走らず、突進を見ないまま終わる
    const bool stalled = !m_slam.justStarted && stepDistance < NS::Core::k_Epsilon;
    m_slam.justStarted = false;

    if (m_slam.travelled >= m_slam.distanceTarget || stalled)
    {
        EndBodySlam();
    }
}

NS::Core::Vector3 Player::AimDirection() const noexcept
{
    const NS::Game::Entity::EntityComponent& body = *m_body;
    NS::Core::Vector3 dir{DesiredDirection().x, 0.0f, DesiredDirection().z};
    float length = std::sqrt(dir.x * dir.x + dir.z * dir.z);

    // 反発後の滑りなど残った速度が向きに勝つと狙いと食い違う方へ飛ぶ。入力が無ければ速度よりカメラの前を先に見る
    if (length < NS::Core::k_Epsilon && GetCameraManager() != nullptr)
    {
        const NS::Core::Vector3 forward = NS::Obj::CameraForwardHorizontal(*this);
        dir = NS::Core::Vector3{forward.x, 0.0f, forward.z};
        length = std::sqrt(dir.x * dir.x + dir.z * dir.z);
    }
    if (length < NS::Core::k_Epsilon)
    {
        const NS::Core::Vector3 lateral = body.LateralVelocity();
        dir = NS::Core::Vector3{lateral.x, 0.0f, lateral.z};
        length = std::sqrt(dir.x * dir.x + dir.z * dir.z);
    }
    if (length < NS::Core::k_Epsilon)
    {
        return NS::Core::Vector3{0.0f, 0.0f, 0.0f};
    }

    return NS::Core::Vector3{dir.x / length, 0.0f, dir.z / length};
}

void Player::MarkBodySlamAim() noexcept
{
    m_request.aimDir = AimDirection();
    m_request.aimAge = 0.0f;
}

float Player::BodySlamAimBlend01() const noexcept
{
    const float hold = m_params->m_slamAimHoldTime;
    const float fade = m_params->m_slamAimFadeTime;
    const float age = m_request.aimAge;
    if (age <= hold)
    {
        return 1.0f;
    }
    // 巻き戻し秒を消える秒より後ろにできる。幅が 0 以下なら割らずに切る
    if (!(fade > hold) || age >= fade)
    {
        return 0.0f;
    }
    return (fade - age) / (fade - hold);
}

bool Player::BodySlam() noexcept
{
    NS::Game::Entity::EntityComponent& body = *m_body;
    NS::Core::Vector3 dir = AimDirection();

    const float aimLength =
        std::sqrt(m_request.aimDir.x * m_request.aimDir.x + m_request.aimDir.z * m_request.aimDir.z);
    // 添えた向きは放す前に見せていた狙いなので、入力も押したフレームの控えも混ぜない
    if (m_request.hasDir)
    {
        dir = m_request.dir;
    }
    else if (aimLength >= NS::Core::k_Epsilon)
    {
        const float blend = BodySlamAimBlend01();
        if (blend > 0.0f)
        {
            NS::Core::Vector3 mixed{dir.x * (1.0f - blend) + m_request.aimDir.x * blend,
                                    0.0f,
                                    dir.z * (1.0f - blend) + m_request.aimDir.z * blend};
            const float mixedLength = std::sqrt(mixed.x * mixed.x + mixed.z * mixed.z);
            // 正反対の向きを同じくらいの重みで混ぜると長さが 0 近くになる。その時は濃い側をそのまま採る
            if (mixedLength >= NS::Core::k_Epsilon)
            {
                dir = NS::Core::Vector3{mixed.x / mixedLength, 0.0f, mixed.z / mixedLength};
            }
            else if (blend >= 0.5f)
            {
                dir = m_request.aimDir;
            }
        }
    }

    if (std::sqrt(dir.x * dir.x + dir.z * dir.z) < NS::Core::k_Epsilon)
    {
        return false;
    }

    // 溜めている間に寄せた分は、控えた相手を放す向きから測り直して乗せる。突進中の寄せはその続きから数える。
    // タップは短い踏み込みの移動技なので、溜めた分も乗せない
    const bool isTap = !(m_request.charge01 > 0.0f);
    float releaseHoming = 0.0f;
    if (!isTap)
    {
        releaseHoming = HomingAngleForRelease(dir);
    }
    dir = NS::Game::Player::RotateHorizontal(dir, NS::Core::ToRadians(NS::Core::Degrees{releaseHoming}).value);
    m_slam.dir = dir;
    m_slam.charge01 = m_request.charge01;
    m_slam.isTap = isTap;
    m_slam.travelled = 0.0f;
    m_slam.justStarted = true;

    if (m_slam.isTap)
    {
        m_slam.distanceTarget = m_params->m_tapSlamDistance;
        body.SetVelocity(NS::Core::Vector3{
            dir.x * m_params->m_tapSlamSpeed, m_params->m_tapSlamUpSpeed, dir.z * m_params->m_tapSlamSpeed});
    }
    else
    {
        m_slam.distanceTarget = m_params->m_bodySlamDistance;
        body.SetVelocity(NS::Core::Vector3{
            dir.x * m_params->m_bodySlamSpeed, body.VerticalVelocity(), dir.z * m_params->m_bodySlamSpeed});
    }

    // 距離が 0 以下だと 1 フレーム目で終わって発動が消えるため、出さずに通常移動のままにする
    if (!(m_slam.distanceTarget > 0.0f))
    {
        return false;
    }

    // 反動の後のカメラが当てた相手の方を向くのに使う
    // 突進の間の寄せで曲がる前の向きを残す
    m_slam.startDir = dir;

    // 控えた相手は放す時に使い切る。突進中は突進の向きから探し直した相手へ寄せる
    ForgetHoming();
    m_homing.angle = releaseHoming;
    m_request.hasDir = false;
    m_request.spent = true;
    // 突進はどの経路で出ても玉で走らせる。掴まり中に放した押しは予約に残り、先行入力の秒の内に
    // 縁を離れれば出るが、その時の丸まりは掴まりで解けている
    ChangeCurled(true);

    (void)m_states->Change<NS::Game::Player::BodySlamPlayerState>(*this);
    m_playerEvents.onBodySlamStarted.Invoke();
    return true;
}

bool Player::ComputeHomingStep(const NS::Core::Vector3& targetCenter,
                               const NS::Core::Vector3& chargeAim,
                               float& nextAngle) const noexcept
{
    const bool rushing = IsBodySlamming();
    if (rushing && m_slam.isTap)
    {
        return false;
    }
    NS::Core::Vector3 base = chargeAim;
    float baseAngle = 0.0f;
    float currentAngle = m_homing.angle;
    if (rushing)
    {
        base = m_slam.dir;
        baseAngle = m_homing.angle;
    }
    NS::Core::Vector3 baseDir{};
    NS::Core::Vector3 toTarget{};
    if (!NS::Core::TryNormalizeHorizontal(base, baseDir) ||
        !NS::Core::TryNormalizeHorizontal(targetCenter - Root().Position(), toTarget))
    {
        return false;
    }
    const float relative =
        NS::Core::ToDegrees(NS::Core::Radians{NS::Game::Player::HorizontalAngleBetween(baseDir, toTarget)}).value;
    if (!std::isfinite(relative))
    {
        return false;
    }
    if (!rushing && m_homing.hasTarget && !(m_homing.target == targetCenter))
    {
        currentAngle = 0.0f;
    }
    const float limit = std::max(0.0f, m_params->m_homingMaxDegrees);
    const float goal = NS::Core::Clamp(baseAngle + relative, -limit, limit);
    const float step = std::max(0.0f, m_params->m_homingStepDegrees);
    nextAngle = std::max(goal, currentAngle - step);
    if (goal > currentAngle)
    {
        nextAngle = std::min(goal, currentAngle + step);
    }
    return true;
}

NS::Core::Vector3 Player::PredictHomingVelocity(const NS::Core::Vector3& targetCenter) const noexcept
{
    const NS::Game::Entity::EntityComponent& body = *m_body;
    float nextAngle = 0.0f;
    if (!IsBodySlamming() || !ComputeHomingStep(targetCenter, m_slam.dir, nextAngle))
    {
        return BodySlamVelocity();
    }
    const NS::Core::Vector3 direction = NS::Game::Player::RotateHorizontal(
        m_slam.dir, NS::Core::ToRadians(NS::Core::Degrees{nextAngle - m_homing.angle}).value);
    return NS::Core::Vector3{
        direction.x * m_params->m_bodySlamSpeed, body.VerticalVelocity(), direction.z * m_params->m_bodySlamSpeed};
}

void Player::SteerToward(const NS::Core::Vector3& targetCenter,
                         float coneDegrees,
                         const NS::Core::Vector3& chargeAim) noexcept
{
    float nextAngle = 0.0f;
    if (!ComputeHomingStep(targetCenter, chargeAim, nextAngle))
    {
        return;
    }
    const float change = nextAngle - m_homing.angle;
    m_homing.angle = nextAngle;
    if (!IsBodySlamming())
    {
        m_homing.target = targetCenter;
        m_homing.coneDegrees = coneDegrees;
        m_homing.hasTarget = true;
        return;
    }
    m_slam.dir = NS::Game::Player::RotateHorizontal(m_slam.dir, NS::Core::ToRadians(NS::Core::Degrees{change}).value);
}

void Player::ApplyBodySlamHeading() noexcept
{
    NS::Game::Entity::EntityComponent& body = *m_body;
    if (IsBodySlamming() && !m_slam.isTap)
    {
        body.SetLateralVelocity(NS::Core::Vector3{
            m_slam.dir.x * m_params->m_bodySlamSpeed, 0.0f, m_slam.dir.z * m_params->m_bodySlamSpeed});
    }
}

void Player::ForgetHoming() noexcept
{
    m_homing.angle = 0.0f;
    m_homing.hasTarget = false;
}

float Player::HomingAngleForRelease(const NS::Core::Vector3& releaseDir) const noexcept
{
    if (!m_homing.hasTarget)
    {
        return 0.0f;
    }
    NS::Core::Vector3 toTarget{};
    if (!NS::Core::TryNormalizeHorizontal(m_homing.target - Root().Position(), toTarget))
    {
        return 0.0f;
    }
    const float relative =
        NS::Core::ToDegrees(NS::Core::Radians{NS::Game::Player::HorizontalAngleBetween(releaseDir, toTarget)}).value;
    // 探した角度の外の相手へ回すと、狙っていない相手へ引かれる。放す向きが溜めていた狙いと違う時に起きる
    if (!(std::abs(relative) <= m_homing.coneDegrees))
    {
        return 0.0f;
    }
    // 累計は溜めていた狙いから測った角度なので、符号は使わず大きさだけを溜めた量として使う
    const float earned = std::abs(m_homing.angle);
    return NS::Core::Clamp(relative, -earned, earned);
}

bool Player::BeginRebound(const NS::Game::Player::ReboundArc& arc) noexcept
{
    // 曲線にならない反動で移すと、弾かれないまま速度が 0 に消える。移さずに偽を返し、速度は呼び手に任せる
    const NS::Core::Vector3 velocity = ReboundVelocityFor(arc);
    if (!(velocity.y > 0.0f))
    {
        return false;
    }
    NS::Core::Vector3 direction{};
    if (!NS::Core::TryNormalizeHorizontal(arc.direction, direction))
    {
        return false;
    }

    m_rebound.direction = direction;
    m_body->SetVelocity(velocity);
    (void)m_states->Change<NS::Game::Player::ReboundPlayerState>(*this);
    return true;
}

NS::Core::Vector3 Player::ReboundVelocityFor(const NS::Game::Player::ReboundArc& arc) const noexcept
{
    // 下りは普段の落ち方のままにする
    // 曲線は下りの重力を上りの重力に対する倍率で持つので、下降重力を上りの重力で割る
    const float riseGravity = -m_params->m_gravityUp * m_params->m_reboundRiseGravityScale;
    const NS::Game::Level::LaunchArc launchArc{.direction = arc.direction,
                                               .distance = arc.distance,
                                               .apexHeight = arc.apexHeight,
                                               .riseGravity = riseGravity,
                                               .fallGravityScale = -m_params->m_gravityDown / riseGravity,
                                               .apexBandSpeed = m_params->m_apexHangVy,
                                               .apexBandGravityScale = m_params->m_apexHangScale};
    return NS::Game::Level::LaunchArcInitialVelocity(launchArc);
}

void Player::SetCurled(bool curled) noexcept
{
    // 掴まりからは突進が出ない。玉のままぶら下がると、押しても突進が出ないのに玉の見た目だけが残る
    // 掴まっている間に玉にすると縁を測り直す手の高さが下がり、押したフレームに縁を放して 0.5 m 落ちた
    if (curled && (m_states->IsCurrent<NS::Game::Player::LedgeHangingPlayerState>() ||
                   m_states->IsCurrent<NS::Game::Player::LedgeClimbingPlayerState>()))
    {
        return;
    }
    ChangeCurled(curled);
}

void Player::ChangeCurled(bool curled) noexcept
{
    NS::Game::Entity::EntityComponent& body = *m_body;
    if (curled == m_curled)
    {
        return;
    }
    m_curled = curled;
    // 立ち姿へ戻った後まで寄せた角度を残すと、次の溜めへ持ち越す
    if (!curled)
    {
        ForgetHoming();
    }
    body.SetSphereShape(curled);
    // 立ち姿の下端は 中心 − 半長 − 半径、玉の下端は 中心 − 半径。中心を立ち姿の半長ぶん上げ下げすると下端が揃う
    // 下げずに玉にすると、当たりの下端が半長ぶん上がる
    float rise = body.StandingHalfHeight();
    if (curled)
    {
        rise = -rise;
    }
    // TODO: 低い天井の下で立ち姿へ戻す時の検査は無い。コースに低い天井が無いうちは、
    // 作り直したキャラクターの食い込みは次の Step の接触の解決に任せる
    // 形の持ち替えは動きではないので、前フレームの位置も一緒にずらす。今の位置だけを動かすと、持ち替えたフレームの
    // 描画の補間で玉が床から浮き (立ち姿は床へ沈み)、立ち姿の中心を見る追従カメラの注視点も半長ぶん揺れる
    Root().ShiftPosition(NS::Core::Vector3{0.0f, rise, 0.0f});
}

void Player::SetBodySlamHeld(bool held) noexcept
{
    m_bodySlamHeld = held;
}

void Player::UncurlWhenSettled() noexcept
{
    NS::Game::Entity::EntityComponent& body = *m_body;
    if (!m_curled)
    {
        return;
    }
    if (m_bodySlamHeld || IsBodySlamming() || !body.IsGrounded())
    {
        return;
    }
    // 反動が明けたフレームは接地の印が残ったまま上向きの速度が入る。速度を見ないと宙へ出る前に解ける
    if (body.VerticalVelocity() > 0.0f)
    {
        return;
    }
    // 当てたフレームは ImpactResolver が自分より先に突進を終える。今の状態だけを見ると、当てた瞬間に解ける
    if (m_slam.wasSlamming)
    {
        return;
    }
    // 放したフレームに出せなかった突進は予約に残る。解くと、予約から出るまでの間だけ立ち姿に戻る
    if (m_request.bufferRemaining > 0.0f)
    {
        return;
    }
    ChangeCurled(false);
}

void Player::PrepareStateStep()
{
    const bool locomotion =
        NS::Game::Player::PlayerJudgeLocomotion::Judge(m_states->IsCurrent<NS::Game::Player::IdlePlayerState>(),
                                                       m_states->IsCurrent<NS::Game::Player::WalkPlayerState>(),
                                                       m_states->IsCurrent<NS::Game::Player::FallPlayerState>(),
                                                       m_states->IsCurrent<NS::Game::Player::ReboundPlayerState>());
    if (NS::Game::Player::PlayerJudgeBodySlam::Judge(
            m_request.bufferRemaining, m_request.spent, m_slam.wasSlamming, locomotion))
    {
        if (BodySlam())
        {
            m_request.bufferRemaining = 0.0f;
        }
    }
}

void Player::FinishStateStep(float dt)
{
    UncurlWhenSettled();
    m_prevJumpHeld = m_input->JumpHeld();
    m_input->ConsumePressed();
    if (m_request.bufferRemaining > 0.0f && !IsBodySlamming())
    {
        m_request.bufferRemaining = std::max(0.0f, m_request.bufferRemaining - dt);
    }
    if (m_request.aimAge < m_params->m_slamAimFadeTime)
    {
        m_request.aimAge += dt;
    }
}

// ---- 崖つかまり ----
// 縁の検出・つかまり・登り

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
    NS::Game::Entity::EntityComponent& body = *m_body;
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
        ChangeCurled(false);
        Root().SetPosition(hang);
        body.SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
        m_ledgeTopY = top;
        m_ledgeFaceNormal = faceNormal;
        (void)m_states->Change<NS::Game::Player::LedgeHangingPlayerState>(*this);
        m_playerEvents.onLedgeGrabbed.Invoke();
        return true;
    }
    return false;
}

bool Player::HoldLedge() noexcept
{
    NS::Game::Entity::EntityComponent& body = *m_body;
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

    NS::Game::Entity::EntityComponent& body = *m_body;
    body.SetVerticalVelocity(m_params->m_jumpImpulse);
    body.SetGrounded(false);
    (void)m_states->Change<NS::Game::Player::FallPlayerState>(*this);
    m_playerEvents.onJump.Invoke();
    return true;
}

void Player::ClimbLedge() noexcept
{
    NS::Game::Entity::EntityComponent& body = *m_body;
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
    (void)m_states->Change<NS::Game::Player::LedgeClimbingPlayerState>(*this);
    body.SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
    m_playerEvents.onLedgeClimbing.Invoke();
}

void Player::DropLedge() noexcept
{
    NS::Game::Entity::EntityComponent& body = *m_body;
    (void)m_states->Change<NS::Game::Player::FallPlayerState>(*this);
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
    NS::Game::Entity::EntityComponent& body = *m_body;
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
        (void)m_states->Change<NS::Game::Player::IdlePlayerState>(*this);
        body.SetGrounded(true);
        m_jumpsRemaining = 1;
        m_coyoteTimer = m_params->m_coyoteTime;
    }
}

bool Player::FindLedgeTopAt(const NS::Core::Vector3& hangPos, float& outTop) const noexcept
{
    const NS::Game::Entity::EntityComponent& body = *m_body;
    const NS::Core::Vector3 inward{-m_ledgeFaceNormal.x, 0.0f, -m_ledgeFaceNormal.z};
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
            hangPos.x - m_ledgeFaceNormal.x * mantleStep,
            top + body.CapsuleHalfHeight(),
            hangPos.z - m_ledgeFaceNormal.z * mantleStep,
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
