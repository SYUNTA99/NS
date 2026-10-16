#include "Game/PlayerActor.h"

#include "Game/Level/CourseDirector.h"
#include "Game/Level/ImpactOutcome.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/SensorKinds.h"
#include "Game/Level/SlamArrow.h"
#include "Game/Level/TargetMarker.h"
#include "Game/Player/ChargeEffects.h"
#include "Game/Player/ImpactEffects.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerClips.h"
#include "Game/Player/PlayerJudges.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/States/BodySlamPlayerState.h"
#include "Game/Player/States/BrakePlayerState.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/LedgeClimbingPlayerState.h"
#include "Game/Player/States/LedgeHangingPlayerState.h"
#include "Game/Player/States/ReboundPlayerState.h"
#include "Game/Player/States/SkidPlayerState.h"
#include "Game/Player/States/WalkPlayerState.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/IUse/IUseSceneSystem.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SubObjects/Animation.h"
#include "NSlib/CoreObject/SubObjects/Body.h"
#include "NSlib/CoreObject/SubObjects/Collider.h"
#include "NSlib/CoreObject/SubObjects/EffectKeeper.h"
#include "NSlib/CoreObject/SubObjects/HitReaction.h"
#include "NSlib/CoreObject/SubObjects/HitSensor.h"
#include "NSlib/CoreObject/SubObjects/Model.h"
#include "NSlib/CoreObject/SubObjects/PlayerInput.h"
#include "NSlib/CoreObject/SubObjects/Shadow.h"
#include "NSlib/CoreObject/SubObjects/Transform.h"

#include <algorithm>
#include <cmath>

NS_REFLECT_BEGIN_IMPL(PlayerActor, NS::SceneActor)
NS_REFLECT_SUBOBJ(m_appearance, "Appearance")
NS_REFLECT_SUBOBJ(m_body, "Movement")
NS_REFLECT_SUBOBJ(m_collider, "Collider")
NS_REFLECT_SUBOBJ(m_input, "Input")
NS_REFLECT_SUBOBJ(m_params, "Params")
NS_REFLECT_SUBOBJ(m_clips, "Clips")
NS_REFLECT_SUBOBJ(m_resolver, "ImpactResolver")
NS_REFLECT_SUBOBJ(m_targetMarker, "TargetMarker")
NS_REFLECT_SUBOBJ(m_slamArrow, "SlamArrow")
NS_REFLECT_SUBOBJ(m_chargeEffects, "ChargeEffects")
NS_REFLECT_SUBOBJ(m_impactEffects, "ImpactEffects")
NS_REFLECT_END_IMPL()

NS_CLASS(PlayerActor)

PlayerActor::PlayerActor() noexcept = default;

PlayerActor::~PlayerActor() = default;
void PlayerActor::OnInit()
{
    NS::Model* model = CreateSubObj(m_model, this);
    model->SetMaterialRef("player");
    model->SetBaseColor(NS::Color{0.5f, 0.5f, 0.5f});
    CreateSubObj(m_appearance, this);
    CreateSubObj(m_body, this);
    CreateSubObj(m_collider, this);
    CreateSubObj(m_input, this);
    CreateSubObj(m_params);
    CreateSubObj(m_clips);
    CreateSubObj(m_shadow, this);
    // 範囲が照合する体は移動の当たりと同じカプセル。寸法の正は Collider の欄で、センサーは毎回それを読む
    NS::FollowHitSensor* bodySensor =
        CreateSubObj<NS::HitSensor, NS::FollowHitSensor>(m_bodySensor, this, [collider = m_collider.get()] {
            return NS::SensorVolume::Capsule(collider->WorldCapsule());
        });
    GL::Level::SetSensorKind(*bodySensor, GL::Level::SensorKind::PlayerBody);
    CreateSubObj(m_resolver, this);
    CreateSubObj(m_hitReaction, this);
    CreateSubObj(m_targetMarker, this);
    CreateSubObj(m_slamArrow, this);
    CreateSubObj(m_effectKeeper, this);
    CreateSubObj(m_chargeEffects, this);
    CreateSubObj(m_impactEffects, this);
    // 部品を全部作った後に組む。先頭の立ちの OnEnter が触る物が揃っている。並べた型が移れる状態の全部になる
    (void)BuildStateMachine<PlayerActor,
                            GL::Player::IdlePlayerState,
                            GL::Player::WalkPlayerState,
                            GL::Player::FallPlayerState,
                            GL::Player::LedgeHangingPlayerState,
                            GL::Player::LedgeClimbingPlayerState,
                            GL::Player::BodySlamPlayerState,
                            GL::Player::BrakePlayerState,
                            GL::Player::ReboundPlayerState,
                            GL::Player::SkidPlayerState>(*this, m_states);
}

GL::Player::CameraTargetState PlayerActor::GetCameraTargetState() const
{
    GL::Player::CameraTargetState state{};
    state.grounded = m_body->IsGrounded();
    state.velocity = m_body->Velocity();
    // 当たりの足元に立ち姿のカプセルを立てた時の中心を見る。玉の間は根が立ち姿の半長ぶん下がっているので、
    // 根を見ると押すたびに画面が 1 フレームで半長ぶん沈み、解けると跳ね上がる
    state.heightOffset = m_collider->StandingHalfHeight() - m_collider->CapsuleHalfHeight();
    // 体当たりから止めの明けまで、カメラは距離と溜めの締めを保つ。当たる瞬間に画面が引かない
    state.framingHeld = IsBodySlamming() || m_resolver->Answer().holdingPlayer;
    state.hasRebound = true;
    state.rebound = NS::FollowReboundDesc{
        .rebounding = IsRebounding(),
        .forcedSlamming = IsBodySlamming() && m_slam.m_forced,
        .slamDirection = BodySlamDirection(),
    };
    // 真ん中の反動は、飛んでいく相手を画面に残す。外れはいつもどおり自機だけを追う (外れのカメラは寄り無し)
    const GL::Level::ImpactRecord& impact = m_resolver->Answer().lastImpact;
    // 外れはカメラを突進の向きへ回り込ませない。回り込むと画面が振り回され、自機がどちらへ逸れたかも読めない
    // 後ろへも下げない。下げるのは飛んでいく相手を画面に収めるためで、外れの相手はほとんど飛ばない
    if (IsRebounding() && impact.tier == GL::Level::HitTier::Wide)
    {
        state.rebound.slamDirection = NS::Vector3{};
        state.rebound.pullBack = false;
    }
    if (IsRebounding() && impact.tier == GL::Level::HitTier::Center && OwningScene() != nullptr)
    {
        if (const NS::SceneActor* partner = OwningScene()->Objects().FindByObjectId(impact.targetId))
        {
            state.rebound.partnerPosition = partner->Root().Position();
        }
    }
    // 溜め量は放した後も放した時の値を返し続けるので、押していないフレームは 0 を渡す。溜めすぎで出た後は押していても
    // 溜めの締めと揺れを解く
    const GL::Level::ImpactInputJudge& judge = ChargeJudge();
    state.hasCharge = true;
    state.charge.held = judge.IsHoldingCharge();
    if (state.charge.held)
    {
        state.charge.charge01 = judge.Charge01();
    }
    GL::Level::SlamLineTarget aim{};
    state.charge.hasAimTarget = TryGetAimTarget(aim);
    if (state.charge.hasAimTarget)
    {
        state.charge.aimTargetCenter = NS::Vector3{aim.bounds.Center.x, aim.bounds.Center.y, aim.bounds.Center.z};
        state.charge.aimTargetRadius = NS::MaxAbsComponent(NS::Vector3{aim.bounds.Extents});
    }
    return state;
}

void PlayerActor::UpdateAnimation()
{
    NS::Animation* animation = AnimationSubObj();
    if (animation == nullptr)
    {
        return;
    }

    const float lateralSpeed = m_body->LateralVelocity().Length();
    const NS::Anim::AnimClip* clip = ChooseClip(lateralSpeed).Get();
    if (clip == nullptr)
    {
        clip = m_clips->m_idleClip.Get();
    }
    // 立ちのクリップも読めなければ、今のクリップのまま
    if (clip != nullptr)
    {
        animation->Play(clip);
    }

    animation->SetSpeed(ChoosePlaybackSpeed(animation->CurrentClip(), lateralSpeed));
}

void PlayerActor::ReadInput()
{
    // clang-format off
    if (m_input) { m_input->Update(); }
    // clang-format on
}

void PlayerActor::Update(bool chargeHeld)
{
    m_input->SetSlamHeld(chargeHeld);
    NS::SceneActor::Update();
}

void PlayerActor::ObserveStep()
{
    NS::SceneActor::ObserveStep();
    // 当たりの途中は受け付けない。相手だけが戻ると、走っている当たりのタイムラインが戻った相手を飛ばす
    // 押しは同じ歩の状態の段で消費されるので、当たりの途中の押しは持ち越さない
    const GL::Level::ImpactAnswer& impact = m_resolver->Answer();
    const bool inImpact = impact.hitStopping || impact.awaitingRebound;
    if (m_input->RespawnPressed() && !inImpact)
    {
        if (GL::Level::CourseDirector* director = NS::GetOrCreateSceneSystem<GL::Level::CourseDirector>(this))
        {
            director->RequestRespawnObjects();
        }
    }
    ObserveCharge(m_input->SlamHeld());
    m_resolver->ObserveImpact(SlamMotion(), m_collider.get());
}

void PlayerActor::DecideStep()
{
    AdvanceCharge(BodyDelta());
    m_resolver->StepState(
        SlamMotion(), m_params.get(), m_body.get(), m_collider.get(), ModelSubObj(), HitReactionSubObj());
    ApplyImpactOrders(m_resolver->Answer());
}

void PlayerActor::ApplyImpactOrders(const GL::Level::ImpactAnswer& answer) noexcept
{
    if (answer.cancelBodySlam)
    {
        CancelBodySlam();
    }
    if (!answer.releaseVelocity.has_value())
    {
        return;
    }
    if (answer.reboundArc.has_value())
    {
        if (BeginRebound(*answer.reboundArc))
        {
            return;
        }
        // 欄が曲線にならない値の時だけ通る。書かないと、止める前の最後のフレームの速度のまま動き出す
        NS_LOG_WARN(Game,
                    "反動が曲線にならず、自機を弾けなかった: 高さ {} 距離 {}",
                    answer.reboundArc->apexHeight,
                    answer.reboundArc->distance);
    }
    m_body->SetVelocity(*answer.releaseVelocity);
}

void PlayerActor::StateStep()
{
    const float dt = BodyDelta();
    if (CanMoveBody() && dt > 0.0f)
    {
        PrepareStateStep();
        // clang-format off
        if (m_stateMachine) { m_stateMachine->Update(); }
        // clang-format on
        FinishStateStep(dt);
    }
    else
    {
        // 身体の段にも止まっている時の押下の消費を置くと、dt が 0 以下の時に 2 回走る。消費は冪等なのでここ 1 回にする
        SkipBodyStep();
    }
}

void PlayerActor::BodyStep()
{
    const float dt = BodyDelta();
    if (CanMoveBody() && dt > 0.0f)
    {
        MoveBody(dt);
    }
}

void PlayerActor::VisualStep()
{
    UpdateAnimation();
    // clang-format off
    m_targetMarker->Update(m_charge);
    m_slamArrow->Update(m_charge, m_params.get(), m_collider.get());
    // clang-format on
    // 震えの振れ幅はカメラとの距離で決まるので、体を動かした後の根の位置で書く
    m_resolver->WriteTremor(m_collider.get(), ModelSubObj());
    const GL::Player::PlayerMotion motion = Motion();
    const GL::Level::ImpactAnswer& impact = m_resolver->Answer();
    // clang-format off
    m_appearance->Update(motion, m_charge, m_slam, m_rebound, m_skid, AimDirection(), m_body.get(), impact);
    m_chargeEffects->Update(m_charge, m_slam, motion, ChargeAimDirection(), m_appearance.get(), impact);
    m_impactEffects->Update(m_slam, motion, m_body.get(), m_collider.get(), impact);
    // clang-format on
#if !defined(NS_SHIPPING)
    DrawChargeRing();
#endif
}

const NS::AssetRef<const NS::Anim::AnimClip>& PlayerActor::ChooseClip(float lateralSpeed) const noexcept
{
    if (m_states->IsCurrent<GL::Player::LedgeHangingPlayerState>())
    {
        if (!m_clips->m_ledgeHangClip.Name().empty())
        {
            return m_clips->m_ledgeHangClip;
        }
        return m_clips->m_idleClip;
    }

    if (!m_body->IsGrounded())
    {
        if (m_body->VerticalVelocity() > 0.0f && !m_clips->m_jumpClip.Name().empty())
        {
            return m_clips->m_jumpClip;
        }
        if (m_body->VerticalVelocity() <= 0.0f && !m_clips->m_fallClip.Name().empty())
        {
            return m_clips->m_fallClip;
        }
        return m_clips->m_idleClip;
    }

    if (lateralSpeed <= NS::k_Epsilon)
    {
        return m_clips->m_idleClip;
    }

    const float maxSpeed = MaxSpeed();
    if (maxSpeed > 0.0f && lateralSpeed >= maxSpeed * m_clips->m_runBlendRatio && !m_clips->m_runClip.Name().empty())
    {
        return m_clips->m_runClip;
    }
    if (!m_clips->m_walkClip.Name().empty())
    {
        return m_clips->m_walkClip;
    }
    return m_clips->m_idleClip;
}

float PlayerActor::ChoosePlaybackSpeed(const NS::Anim::AnimClip* clip, float lateralSpeed) const noexcept
{
    if (clip == nullptr || (clip != m_clips->m_walkClip.Get() && clip != m_clips->m_runClip.Get()))
    {
        return 1.0f;
    }

    const float maxSpeed = MaxSpeed();
    if (maxSpeed <= 0.0f)
    {
        return 1.0f;
    }
    return std::max(m_clips->m_minPlaybackSpeed, lateralSpeed / maxSpeed);
}

void PlayerActor::OnAssetsLoaded()
{
    NS::SceneActor::OnAssetsLoaded();
    // Model は自分のメッシュの欄を読み直したところなので、今の姿のメッシュを差し直す
    m_appearance->ShowCurrentLook();
}

void PlayerActor::OnStart()
{
    NS::SceneActor::OnStart();
    if (NS::Scene* scene = OwningScene())
    {
        scene->RegisterOverlay(m_targetMarker.get());
        scene->RegisterOverlay(m_slamArrow.get());
    }
    m_chargeEffects->Preload();
    m_impactEffects->Preload();
}

void PlayerActor::OnEndPlay()
{
    m_states->Reset();
    // 次のプレイで選んだクリップを頭から流す
    if (NS::Animation* animation = AnimationSubObj())
    {
        animation->Play(nullptr);
    }
    NS::SceneActor::OnEndPlay();
    // 走っている当たりのタイムラインを捨てる。プレイの途中で外れても止めが次のプレイへ残らない
    m_resolver->CancelImpact(ModelSubObj(), HitReactionSubObj());
    m_appearance->ResetDrawScale();
    m_chargeEffects->Clear();
    if (NS::Scene* scene = OwningScene())
    {
        scene->UnregisterOverlay(m_targetMarker.get());
        scene->UnregisterOverlay(m_slamArrow.get());
    }
    EndCharge();
}

void PlayerActor::InitAfterPlacement()
{
    ResetHealth();
    // 死とゴールを伝える先。先に作っておくと、最初の知らせの段で流れが進む
    (void)NS::GetOrCreateSceneSystem<GL::Level::CourseDirector>(this);
}

bool PlayerActor::ReceiveMsg(const NS::Message& msg, NS::HitSensor* sender, NS::HitSensor* receiver)
{
    (void)sender;
    (void)receiver;
    if (GL::Level::IsMsgInstantDeath(msg))
    {
        Die();
        if (GL::Level::CourseDirector* director = NS::GetOrCreateSceneSystem<GL::Level::CourseDirector>(this))
        {
            director->NotifyPlayerDead();
        }
        return true;
    }
    if (const GL::Level::MsgGoal* goal = NS::MsgCast<GL::Level::MsgGoal>(msg))
    {
        if (GL::Level::CourseDirector* director = NS::GetOrCreateSceneSystem<GL::Level::CourseDirector>(this))
        {
            director->NotifyGoal(goal->FadeOutSeconds(), goal->FadeInSeconds());
        }
        return true;
    }
    if (const GL::Level::MsgCourseRestart* restart = NS::MsgCast<GL::Level::MsgCourseRestart>(msg))
    {
        RestartFrom(restart->Baseline());
        return true;
    }
    if (const GL::Level::MsgInputLock* lock = NS::MsgCast<GL::Level::MsgInputLock>(msg))
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

void PlayerActor::RestartFrom(const nlohmann::json& baseline) noexcept
{
    // 出現位置は配置したプレイヤーのカプセル中心の世界座標
    // 凍結に既にある値なので写しは持たず、その都度読む。居なければ新規レベルで補う位置へ戻す
    NS::Vector3 spawn = DefaultSpawnPosition();
    const std::size_t index = NS::FindObjectIndexById(baseline, Id());
    if (index != NS::k_NoObjectIndex)
    {
        spawn = NS::ObjectPosition(NS::SceneJsonObjects(baseline)[index]);
    }

    Root().SetPosition(spawn);
    ResetState();
    ResetHealth();
    Appear();
}

void PlayerActor::ApplyDamage(int amount) noexcept
{
    m_health.ApplyDamage(amount);
}

void PlayerActor::Die() noexcept
{
    m_health.Deplete();
    Disappear();
}

void PlayerActor::ResetHealth() noexcept
{
    m_health.SetMaxHealth(m_params->MaxHealth());
    m_health.Reset();
}

bool PlayerActor::IsDead() const noexcept
{
    return m_health.IsDead();
}

int PlayerActor::Health() const noexcept
{
    return m_health.Current();
}

void PlayerActor::SetDesiredMove(const NS::Vector3& worldDir, float speedScale01) noexcept
{
    m_input->SetDesiredMove(worldDir, speedScale01);
}

float PlayerActor::DesiredSpeedScale() const noexcept
{
    return m_input->DesiredSpeedScale();
}

NS::Vector3 PlayerActor::DesiredDirection() const noexcept
{
    return m_input->DesiredDirection();
}

void PlayerActor::SetClimbMove(float localRight, float localForward) noexcept
{
    m_input->SetClimbMove(localRight, localForward);
}

float PlayerActor::ClimbRight() const noexcept
{
    return m_input->ClimbRight();
}

float PlayerActor::ClimbForward() const noexcept
{
    return m_input->ClimbForward();
}

void PlayerActor::SetJumpPressed() noexcept
{
    m_input->SetJumpPressed();
}

void PlayerActor::SetReleaseLedgePressed() noexcept
{
    m_input->SetReleaseLedgePressed();
}

void PlayerActor::SetJumpHeld(bool held) noexcept
{
    m_input->SetJumpHeld(held);
}

float PlayerActor::MaxSpeed() const noexcept
{
    const float capped = m_params->m_runSpeed * m_maxSpeedScale;
    if (capped < 0.0f)
    {
        return 0.0f;
    }
    return capped;
}

void PlayerActor::SetMaxSpeedScale(float scale) noexcept
{
    // 非数を入れると MaxSpeed() との比較が偽になり、突進明けに水平の速さが切られない
    if (!std::isfinite(scale))
    {
        return;
    }
    m_maxSpeedScale = scale;
}

float PlayerActor::RunSpeed() const noexcept
{
    return m_params->m_runSpeed;
}

bool PlayerActor::IsBodySlamming() const noexcept
{
    // 状態機械を組んでいる最中 (先頭の OnEnter) はまだ預かっていないので nullptr を見る
    return m_states != nullptr && m_states->IsCurrent<GL::Player::BodySlamPlayerState>();
}

float PlayerActor::BodySlamProgress01() const noexcept
{
    if (!IsBodySlamming() || !(m_slam.m_distanceTarget > 0.0f))
    {
        return 0.0f;
    }
    return NS::Clamp(m_slam.m_travelled / m_slam.m_distanceTarget, 0.0f, 1.0f);
}

float PlayerActor::BodySlamDistance() const noexcept
{
    return m_params->m_bodySlamDistance;
}

bool PlayerActor::IsRebounding() const noexcept
{
    return m_states != nullptr && m_states->IsCurrent<GL::Player::ReboundPlayerState>();
}

bool PlayerActor::IsSkidding() const noexcept
{
    return m_states != nullptr && m_states->IsCurrent<GL::Player::SkidPlayerState>();
}

bool PlayerActor::SkidsOnLanding() const noexcept
{
    return m_rebound.m_missTumble.has_value() && m_params->m_missSkidSteps > 0;
}

float PlayerActor::SkidSpeedScale() const noexcept
{
    if (!IsSkidding())
    {
        return 1.0f;
    }
    return m_skid.SpeedScale();
}

bool PlayerActor::CanMoveBody() const noexcept
{
    // 当たりの止めの正は裁定役の時計。身体の active へ写すと、やり直しが写しを戻し忘れた時に正と食い違う
    const GL::Level::ImpactAnswer& impact = m_resolver->Answer();
    return IsAppeared() && !impact.hitStopping && !impact.awaitingRebound;
}

GL::Player::PlayerMotion PlayerActor::Motion() const noexcept
{
    return GL::Player::PlayerMotion{
        .bodySlamming = IsBodySlamming(),
        .rebounding = IsRebounding(),
        .skidding = IsSkidding(),
        .curled = m_curled,
        .canMoveBody = CanMoveBody(),
    };
}

void PlayerActor::ResetState() noexcept
{
    m_body->SetVelocity(NS::Vector3{0.0f, 0.0f, 0.0f});
    m_input->ResetMovementInput();
    m_prevJumpHeld = false;
    m_jumpsRemaining = 1;
    m_coyoteTimer = 0.0f;
    m_bufferTimer = 0.0f;
    m_body->SetGrounded(false);
    m_ledgeTopY = 0.0f;
    m_ledgeFaceNormal = NS::Vector3{0.0f, 0.0f, 0.0f};
    m_ledgeMantleTimer = 0.0f;
    m_facingDir = NS::Vector3{0.0f, 0.0f, 0.0f};
    m_lastMoveDistance = 0.0f;
    m_request.bufferRemaining = 0.0f;
    m_request.spent = false;
    m_request.charge01 = 0.0f;
    m_request.overcharge01 = 0.0f;
    m_request.dir = NS::Vector3{0.0f, 0.0f, 0.0f};
    m_request.hasDir = false;
    m_request.verticalSpeed = 0.0f;
    m_slam = GL::Player::BodySlamStatus{};
    m_rebound.m_direction = NS::Vector3{0.0f, 0.0f, 0.0f};
    m_rebound.m_spinSpeed = 0.0f;
    m_rebound.m_missTumble.reset();
    m_skid = GL::Player::SkidStatus{};
    // 当たりの形だけを立ち姿へ戻し、根は動かさない。出直しは根を出現位置へ置いてから呼ぶので、
    // 丸まりを解く時のように根を上げると出現位置より半長ぶん高く湧いた
    m_curled = false;
    m_collider->SetSphereShape(false);
    m_bodySlamHeld = false;
    m_states->Reset();
    // 止めの最中か反動を待つ間のやり直しで、出現位置で明けて弾かれないよう、走っている当たりのタイムラインを持ち主に打ち切らせる
    m_resolver->CancelImpact(ModelSubObj(), HitReactionSubObj());
    // 潰れたままの描く形から出現位置の形へ補間されないよう、前のフレームの倍率ごと揃える
    m_appearance->ResetDrawScale();
}

void PlayerActor::SkipBodyStep() noexcept
{
    m_input->ConsumePressed();
    m_prevJumpHeld = m_input->JumpHeld();
}

void PlayerActor::PrepareStateStep()
{
    const bool locomotion =
        GL::Player::PlayerJudgeLocomotion::Judge(m_states->IsCurrent<GL::Player::IdlePlayerState>(),
                                                 m_states->IsCurrent<GL::Player::WalkPlayerState>(),
                                                 m_states->IsCurrent<GL::Player::FallPlayerState>(),
                                                 m_states->IsCurrent<GL::Player::ReboundPlayerState>());
    if (GL::Player::PlayerJudgeBodySlam::Judge(
            m_request.bufferRemaining, m_request.spent, m_slam.m_wasSlamming, locomotion))
    {
        if (BodySlam())
        {
            m_request.bufferRemaining = 0.0f;
        }
    }
}

void PlayerActor::FinishStateStep(float dt)
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

PlayerActor* FindPlayer(NS::ActorList& objects) noexcept
{
    for (NS::SceneActor* obj : objects)
    {
        if (PlayerActor* player = NS::Cast<PlayerActor>(obj))
        {
            return player;
        }
    }
    return nullptr;
}

// TODO: 床の上面を仮定している。床の高さが違うレベルを作ったら、補う時に下向きに引いて置く
NS::Vector3 PlayerActor::DefaultSpawnPosition() const noexcept
{
    return NS::Vector3{0.0f,
                       m_params->m_spawnFloorTop + Collider().StandingHalfHeight() + Collider().CapsuleRadius() +
                           m_params->m_spawnClearance,
                       0.0f};
}
