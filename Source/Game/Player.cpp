#include "Game/Player.h"

#include "Game/Level/CourseDirector.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/SensorKinds.h"
#include "Game/Level/SlamArrow.h"
#include "Game/Level/TargetMarker.h"
#include "Game/Player/ChargeEffects.h"
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
#include "Runtime/Core/Logger.h"
#include "Runtime/Object/Components/Animation.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/HitReaction.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Components/Model.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/IUse/IUseSceneObj.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Platform/Clock.h"

#include <algorithm>
#include <cmath>
#include <cstring>

NS_CLASS(Player)

Player::Player() noexcept
{
    m_appearance = std::make_unique<NS::Game::Player::PlayerAppearance>();
    m_body = std::make_unique<NS::Obj::Body>();
    m_input = std::make_unique<NS::Obj::PlayerInput>();
    m_params = std::make_unique<NS::Game::Player::PlayerParams>();
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
    // 範囲が照合する体は移動の当たりと同じカプセル。寸法の正は Body の欄で、センサーは毎回それを読む
    SetBodySensorPart(std::make_unique<NS::Obj::FollowHitSensor>(
        [body = m_body.get()] { return NS::Obj::SensorVolume::Capsule(body->WorldCapsule()); }));
    NS::Game::Level::SetSensorKind(*BodySensorPart(), NS::Game::Level::SensorKind::PlayerBody);
    AttachFixedComponent(*m_resolver);
    (void)CreatePart("HitReaction");
    AttachFixedComponent(*m_targetMarker);
    AttachFixedComponent(*m_slamArrow);
    AttachFixedComponent(*m_chargeEffects);
    AttachFixedComponent(*m_impactEffects);
    // 部品を全部付けた後に組む。先頭の立ちの OnEnter が触る物が揃っている。並べた型が移れる状態の全部になる
    (void)BuildStateMachine<Player,
                            NS::Game::Player::IdlePlayerState,
                            NS::Game::Player::WalkPlayerState,
                            NS::Game::Player::FallPlayerState,
                            NS::Game::Player::LedgeHangingPlayerState,
                            NS::Game::Player::LedgeClimbingPlayerState,
                            NS::Game::Player::BodySlamPlayerState,
                            NS::Game::Player::BrakePlayerState,
                            NS::Game::Player::ReboundPlayerState>(*this, m_states);
}

Player::~Player() = default;

void Player::ForEachPart(const PartVisitor& visitor) const
{
    NS::Obj::Actor::ForEachPart(visitor);
    visitor("Appearance", *m_appearance);
    visitor("Movement", *m_body);
    visitor("Input", *m_input);
    visitor("Params", *m_params);
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
        .slamDirection = BodySlamDirection(),
    };
    // 溜め量は放した後も放した時の値を返し続けるので、押していないフレームは 0 を渡す
    const NS::Game::Level::ImpactInputJudge& judge = ChargeJudge();
    state.hasCharge = true;
    state.charge.held = judge.IsHeld();
    if (state.charge.held)
    {
        state.charge.charge01 = judge.Charge01();
    }
    NS::Game::Level::SlamLineTarget aim{};
    state.charge.hasAimTarget = TryGetAimTarget(aim);
    if (state.charge.hasAimTarget)
    {
        state.charge.aimTargetCenter = NS::Core::Vector3{aim.bounds.Center.x, aim.bounds.Center.y, aim.bounds.Center.z};
        state.charge.aimTargetRadius = std::max({aim.bounds.Extents.x, aim.bounds.Extents.y, aim.bounds.Extents.z});
    }
    return state;
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

void Player::Update(bool chargeHeld)
{
    m_input->SetSlamHeld(chargeHeld);
    NS::Obj::Actor::Update();
}

void Player::ObserveStep()
{
    NS::Obj::Actor::ObserveStep();
    ObserveCharge(m_input->SlamHeld());
    if (m_resolver->IsActive())
    {
        m_resolver->ObserveImpact();
    }
}

void Player::DecideStep()
{
    AdvanceCharge(NS::Platform::FrameTimer::FixedDelta());
    if (m_resolver->IsActive())
    {
        m_resolver->StepState();
    }
    else
    {
        // 外された裁定役は止めも予約も捨てる。持ち越すと入れ直した時に残りの止めが明け、遅れて弾かれる
        m_resolver->CancelImpact();
    }
}

void Player::StateStep()
{
    const float dt = NS::Platform::FrameTimer::FixedDelta();
    if (CanMoveBody() && dt > 0.0f)
    {
        PrepareStateStep();
        StepStateMachine();
        FinishStateStep(dt);
    }
    else
    {
        // 身体の段にも止まっている時の押下の消費を置くと、dt が 0 以下の時に 2 回走る。消費は冪等なのでここ 1 回にする
        SkipBodyStep();
    }
}

void Player::BodyStep()
{
    const float dt = NS::Platform::FrameTimer::FixedDelta();
    if (CanMoveBody() && dt > 0.0f)
    {
        MoveBody(dt);
    }
}

void Player::VisualStep()
{
    UpdateAnimation();
    TickPart(m_targetMarker.get());
    TickPart(m_slamArrow.get());
    TickPart(HitReactionPart());
    TickPart(m_appearance.get());
    TickPart(m_chargeEffects.get());
    TickPart(m_impactEffects.get());
#if !defined(NS_SHIPPING)
    DrawChargeRing();
#endif
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
    m_states->Reset();
    m_appliedClip.clear();
    NS::Obj::Actor::OnEndPlay();
    // 部品の OnEndPlay の後に置く。後ろの部品は丸まりも構えも読まないので、捨てる時機はここで足りる
    EndCharge();
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
        m_input->SetLocked(lock->Locked());
        // 押しが偽になった歩を放したと読むと、溜めた突進が出る。止める時は放させずに溜めを捨てる
        if (lock->Locked())
        {
            CancelCharge();
        }
        return true;
    }
    return false;
}

void Player::RestartFrom(const nlohmann::json& baseline) noexcept
{
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

bool Player::CanMoveBody() const noexcept
{
    // 当たりの止めの正は裁定役の数え。身体の active へ写すと、やり直しが写しを戻し忘れた時に正と食い違う
    return m_body->IsActive() && !m_resolver->IsHitStopping();
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
    m_request.verticalSpeed = 0.0f;
    m_slam.charge01 = 0.0f;
    m_slam.travelled = 0.0f;
    m_slam.distanceTarget = 0.0f;
    m_slam.justStarted = false;
    m_slam.dir = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
    m_rebound.direction = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
    // 当たりの形だけを立ち姿へ戻し、根は動かさない。出直しは根を出現位置へ置いてから呼ぶので、
    // 丸まりを解く時のように根を上げると出現位置より半長ぶん高く湧いた
    m_curled = false;
    m_body->SetSphereShape(false);
    m_bodySlamHeld = false;
    m_slam.wasSlamming = false;
    m_states->Reset();
    // 止めの最中か予約の残るやり直しで、出現位置で明けて弾かれないよう止めを持ち主に捨てさせる
    m_resolver->CancelImpact();
    // 潰れたままの描く形から出現位置の形へ補間されないよう、前のフレームの倍率ごと揃える
    m_appearance->ResetDrawScale();
}

void Player::SkipBodyStep() noexcept
{
    m_input->ConsumePressed();
    m_prevJumpHeld = m_input->JumpHeld();
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
