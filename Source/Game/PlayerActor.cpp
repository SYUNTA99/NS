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
#include "Game/Player/States/AirDashPlayerState.h"
#include "Game/Player/States/BeforeContactPlayerState.h"
#include "Game/Player/States/DashReboundPlayerState.h"
#include "Game/Player/States/BodySlamPlayerState.h"
#include "Game/Player/States/BrakePlayerState.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/LedgeClimbingPlayerState.h"
#include "Game/Player/States/LedgeHangingPlayerState.h"
#include "Game/Player/States/ReboundPlayerState.h"
#include "Game/Player/States/SkidPlayerState.h"
#include "Game/Player/States/TackleHitPlayerState.h"
#include "Game/Player/States/WalkPlayerState.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Asset/AnimClip.h"
#include "NSlib/CoreObject/Action/ActionTimeline.h"
#include "NSlib/CoreObject/ClassObjs/Animation.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/EffectKeeper.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/CoreObject/ClassObjs/Shadow.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IGet/IGetSceneSystem.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"

#include <algorithm>
#include <cmath>

NS_REFLECT_BEGIN_IMPL(PlayerActor, NS::SceneActor)
NS_CLASSOBJ(m_appearance, "Appearance")
NS_CLASSOBJ(m_body, "Movement")
NS_CLASSOBJ(m_collider, "Collider")
NS_CLASSOBJ(m_input, "Input")
NS_CLASSOBJ(m_params, "Params")
NS_CLASSOBJ(m_clips, "Clips")
NS_CLASSOBJ(m_resolver, "ImpactResolver")
NS_CLASSOBJ(m_targetMarker, "TargetMarker")
NS_CLASSOBJ(m_slamArrow, "SlamArrow")
NS_CLASSOBJ(m_chargeEffects, "ChargeEffects")
NS_CLASSOBJ(m_impactEffects, "ImpactEffects")
NS_REFLECT_END_IMPL()

NS_PLACEABLE(PlayerActor, "自機")

PlayerActor::PlayerActor() noexcept = default;

PlayerActor::~PlayerActor() = default;
void PlayerActor::OnInit()
{
    CreateClassObj(m_model);
    NS::InitDraw(this, m_model.get(), &Root());
    CreateClassObj(m_appearance);
    CreateClassObj(m_body);
    // シーンは OnInit の前に決まっている。当たりは作る時に地形と根を受け取り、持ち主を持たない
    CreateClassObj(m_collider, GetPhysicsScene(), &Root());
    CreateClassObj(m_input);
    CreateClassObj(m_params);
    CreateClassObj(m_clips);
    CreateClassObj(m_shadow);
    NS::InitDraw(this, m_shadow.get(), &Root(), OwningScene());
    // 残像は本体の Model を元に描く
    NS::InitDraw(this, &m_tackleGhost, &Root(), m_model.get());
    CreateClassObj(m_resolver);
    CreateClassObj(m_hitReaction);
    CreateClassObj(m_actionTimeline);
    CreateClassObj(m_targetMarker);
    NS::InitOverlay(this, m_targetMarker.get());
    CreateClassObj(m_slamArrow);
    NS::InitOverlay(this, m_slamArrow.get());
    CreateClassObj(m_effectKeeper);
    CreateClassObj(m_chargeEffects);
    CreateClassObj(m_impactEffects);
    // 部品を全部作った後に組む。並べた型が移れる状態の全部になる。先頭の立ちへは最初の更新の初めに入る
    (void)BuildStateMachine<PlayerActor,
                            GL::Player::IdlePlayerState,
                            GL::Player::WalkPlayerState,
                            GL::Player::FallPlayerState,
                            GL::Player::LedgeHangingPlayerState,
                            GL::Player::LedgeClimbingPlayerState,
                            GL::Player::BodySlamPlayerState,
                            GL::Player::BeforeContactPlayerState,
                            GL::Player::BrakePlayerState,
                            GL::Player::TackleHitPlayerState,
                            GL::Player::ReboundPlayerState,
                            GL::Player::SkidPlayerState,
                            GL::Player::AirDashPlayerState,
                            GL::Player::DashReboundPlayerState>(this, m_states);
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
    state.framingHeld = IsBodySlamming() || IsHeldByTackleHit();
    state.hasRebound = true;
    state.rebound = NS::FollowReboundDesc{
        .rebounding = IsRebounding(),
        .forcedSlamming = IsBodySlamming() && m_slam.m_forced,
        .slamDirection = BodySlamDirection(),
        .widenView = IsAirDashing() || IsDashRebounding(),
    };
    // 地形の反動はカメラの向きを回さず、回す入力も止めない。スティックの基準がずれず、戻ったダッシュが思った向きへ出る
    // 帯の縦は表で丸めた向きから取るので、同じ当て方なら毎回同じ画面になる
    if (IsDashRebounding())
    {
        state.rebound.rebounding = true;
        state.rebound.slamDirection = NS::Vector3{};
        state.rebound.pullBack = false;
        state.rebound.holdLook = false;
        state.rebound.verticalLean = std::clamp(m_airDash.LastRebound().shifted.y, -1.0f, 1.0f);
    }
    // 真ん中の反動は、飛んでいく相手を画面に残す。外れはいつもどおり自機だけを追う (外れのカメラは寄り無し)
    const GL::Level::ImpactRecord& impact = m_resolver->Answer().lastImpact;
    // 外れはカメラを突進の向きへ回り込ませない。回り込むと画面が振り回され、自機がどちらへ逸れたかも読めない
    // 後ろへも下げない。下げるのは飛んでいく相手を画面に収めるためで、外れの相手はほとんど飛ばない
    if (IsRebounding() && impact.tier == GL::Level::HitTier::Wide)
    {
        state.rebound.slamDirection = NS::Vector3{};
        state.rebound.pullBack = false;
    }
    if (IsRebounding() && impact.tier == GL::Level::HitTier::Center)
    {
        if (const NS::SceneActor* partner = OwningScene()->Actors().FindByActorId(impact.targetId))
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

void PlayerActor::WriteFollowState(NS::ThirdPersonFollow* vcam) const
{
    NS_ASSERT(Game, vcam != nullptr, "PlayerActor::WriteFollowState: 書く先の追従カメラが無い");
    const GL::Player::CameraTargetState state = GetCameraTargetState();
    vcam->SetFollowMotion(state.grounded, state.velocity);
    vcam->SetTargetHeightOffset(state.heightOffset);
    vcam->SetFollowFramingHeld(state.framingHeld);
    if (state.hasRebound && !vcam->SetFollowRebound(state.rebound))
    {
        NS_LOG_WARN(Game,
                    "突進の向きが壊れていて、反動をカメラへ渡さなかった: ({}, {}, {})",
                    state.rebound.slamDirection.x,
                    state.rebound.slamDirection.y,
                    state.rebound.slamDirection.z);
    }
    if (state.hasCharge && !vcam->SetFollowCharge(state.charge))
    {
        NS_LOG_WARN(Game, "溜めの状態が壊れていて、追従カメラへ渡さなかった: 溜め量 {}", state.charge.charge01);
    }
}

void PlayerActor::UpdateAnimation()
{
    NS::Animation* animation = AnimationClassObj();
    if (animation == nullptr)
    {
        return;
    }

    const float lateralSpeed = m_body->LateralVelocity().Length();
    const NS::Anim::AnimClip* clip = ChooseClip(lateralSpeed)->Clip();
    if (clip == nullptr)
    {
        clip = m_clips->m_idleClip->Clip();
    }
    // 立ちのクリップも読めなければ、今のクリップのまま
    if (clip != nullptr)
    {
        animation->Play(clip);
    }

    animation->SetSpeed(ChoosePlaybackSpeed(animation->CurrentClip(), lateralSpeed));
}

void PlayerActor::Update(bool chargeHeld)
{
    m_input->SetSlamHeld(chargeHeld);
    m_inputInjected = true;
    NS::SceneActor::Update();
    m_inputInjected = false;
}

void PlayerActor::DecideStep()
{
    // 世界を進めた固定フレームにだけ回るので、世界を進めない固定フレームの押しもここで拾う
    if (!m_inputInjected)
    {
        m_input->Update(this);
    }
    // 当たりの途中は受け付けない。相手だけが戻ると、明けの知らせが戻った相手を飛ばす
    // ヒットストップの間は押しを下ろさないので、その間の押しは明けのフレームまで残る。明けのフレームも
    // 当たりの途中なのでここでは受け付けず、同じフレームの状態機械の後か押しを下ろす所で消えて、明けの後へ持ち越さない
    if (m_input->RespawnPressed() && !IsHitStopping() && !IsInTackleHit())
    {
        if (GL::Level::CourseDirector* director = NS::GetOrCreateSceneSystem<GL::Level::CourseDirector>(this))
        {
            director->RequestRespawnActors();
        }
    }
    // 体当たりのボタンはエアダッシュに使う。押した瞬間に出すので、溜めには押しを渡さない
    const bool pressHeld = m_input->SlamHeld();
    m_airDash.m_pressed = pressHeld && !m_airDash.m_prevPressHeld;
    m_airDash.m_prevPressHeld = pressHeld;
    UpdateIntendedDirection(NS::GetFrameDelta(this));
    ObserveCharge(false);
    m_resolver->ObserveImpact(SelfForImpact(), SlamMotion(), m_collider.get());

    AdvanceCharge(NS::GetFrameDelta(this));
    // 止めの後に初めて回るフレームが明け。状態の段より前に反動へ移すと、反動の 1 歩目が明けのフレームに回る
    const bool releasing = IsInTackleHit() && !IsHitStopping();
    m_resolver->StepState(
        SelfForImpact(), SlamMotion(), m_params.get(), m_body.get(), m_collider.get(), HitReactionClassObj());
    if (releasing)
    {
        ReleaseTackleHit();
    }
    ApplyImpactOrders(m_resolver->Answer());
    DecideBeforeContact(m_resolver->Answer());
    m_stateStepPrepared = PrepareOrSkipStateStep();
}

bool PlayerActor::PrepareOrSkipStateStep()
{
    // 止めの間は基盤が状態機械を飛ばす。押しは下ろさず、止めの明けへ持ち越す
    if (IsHitStopping())
    {
        return false;
    }
    const float dt = NS::GetFrameDelta(this);
    if (CanMoveBody() && dt > 0.0f)
    {
        PrepareStateStep();
        return true;
    }
    // 体の段にも止まっている時の押下の消費を置くと、dt が 0 以下の時に 2 回走る。消費は冪等なのでここ 1 回にする
    DropPresses();
    // 当たりの状態は体を動かさないので、突進の発動と丸まりの解除を通さずに状態機械だけを進める。
    // 入る予約の確定 (段の束を始める) は止めの前のフレームに回る
    if (!(IsInTackleHit() && dt > 0.0f))
    {
        SkipStateStep();
    }
    return false;
}

void PlayerActor::AfterStateStep()
{
    if (m_stateStepPrepared)
    {
        FinishStateStep(NS::GetFrameDelta(this));
    }
}

void PlayerActor::ApplyImpactOrders(const GL::Level::ImpactAnswer& answer) noexcept
{
    if (answer.cancelBodySlam)
    {
        // 新しい当たりは、前の当たりの束で走っている演出を打ち切る。当たる直前から続く当たりは入る時に打ち切り、
        // 当たる直前の動作の形を検知のフレームまで描く
        if (!ContinuesBeforeContact(answer.lastImpact))
        {
            StopTackleHitEffects();
            if (NS::HitReaction* reaction = HitReactionClassObj())
            {
                reaction->Stop(this);
            }
        }
        CancelBodySlam();
    }
    // 検知のフレームは歩きか落下の 1 歩を回し、次のフレームから止める
    if (answer.beginHit)
    {
        (void)m_states->Change<GL::Player::TackleHitPlayerState>();
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
    // 貫通と反動にならない明けは、当たりの状態を出て通常の移動へ戻る
    if (IsInTackleHit())
    {
        ChangeToWalkOrFall();
    }
}

void PlayerActor::BodyStep()
{
    const float dt = NS::GetFrameDelta(this);
    if (CanMoveBody() && dt > 0.0f)
    {
        MoveBody(dt);
    }
}

void PlayerActor::VisualStep()
{
    // 写し取る姿は Model が揺れを書いた後で、見た目の部品が回転と倍率を書く前の姿
    m_ghostRun.Advance(&m_tackleGhost, m_model.get(), Root().WorldMatrix());
    UpdateAnimation();
    // clang-format off
    m_targetMarker->Update(m_charge);
    m_slamArrow->Update(m_charge, m_params.get(), m_collider.get());
    // clang-format on
    const GL::Player::PlayerMotion motion = Motion();
    const GL::Level::ImpactAnswer& impact = m_resolver->Answer();
    // clang-format off
    const NS::Vector3 release = ReleaseDirection();
    const NS::Vector3 center = Root().Position();
    m_appearance->Update(m_model.get(), NS::GetFrameDelta(this),
                         motion, m_charge, m_slam, m_rebound, m_skid, release, impact);
    m_chargeEffects->Update(this, center, m_charge, m_slam, motion, release, m_appearance.get(), impact);
    m_impactEffects->Update(this, this, center, m_slam, motion, m_body.get(), m_collider.get(), impact.lastImpact);
    // clang-format on
#if !defined(NS_SHIPPING)
    DrawChargeRing();
#endif
    DrawTerrainRebound();
}

const NS::AnimClip* PlayerActor::ChooseClip(float lateralSpeed) const noexcept
{
    if (m_states->IsCurrent<GL::Player::LedgeHangingPlayerState>())
    {
        if (!m_clips->m_ledgeHangClip->Path().empty())
        {
            return m_clips->m_ledgeHangClip;
        }
        return m_clips->m_idleClip;
    }

    if (!m_body->IsGrounded())
    {
        if (m_body->VerticalVelocity() > 0.0f && !m_clips->m_jumpClip->Path().empty())
        {
            return m_clips->m_jumpClip;
        }
        if (m_body->VerticalVelocity() <= 0.0f && !m_clips->m_fallClip->Path().empty())
        {
            return m_clips->m_fallClip;
        }
        return m_clips->m_idleClip;
    }

    if (NS::IsNearlyZero(lateralSpeed))
    {
        return m_clips->m_idleClip;
    }

    const float maxSpeed = MaxSpeed();
    if (maxSpeed > 0.0f && lateralSpeed >= maxSpeed * m_clips->m_runBlendRatio && !m_clips->m_runClip->Path().empty())
    {
        return m_clips->m_runClip;
    }
    if (!m_clips->m_walkClip->Path().empty())
    {
        return m_clips->m_walkClip;
    }
    return m_clips->m_idleClip;
}

float PlayerActor::ChoosePlaybackSpeed(const NS::Anim::AnimClip* clip, float lateralSpeed) const noexcept
{
    if (clip == nullptr || (clip != m_clips->m_walkClip->Clip() && clip != m_clips->m_runClip->Clip()))
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
    // Model は自分のメッシュの欄を読み直したところなので、当たりの形に合う姿のメッシュを差し直す
    if (IsCurled())
    {
        m_appearance->Curl(m_model.get());
    }
    else
    {
        m_appearance->Uncurl(m_model.get());
    }
    m_chargeEffects->Preload(this);
    m_impactEffects->Preload(this);
}

void PlayerActor::OnEndPlay()
{
    // 戻す物はやり直しと同じ組。当たりの止めと状態と演出も捨て、止めが次のプレイへ残らない
    ResetState();
    // 次のプレイで選んだクリップを頭から流す
    if (NS::Animation* animation = AnimationClassObj())
    {
        animation->Play(nullptr);
    }
    // 残像の値は ResetState が消してある。描く物は消えていても入れたままなので、基底と同じくここで外す
    NS::SceneActor::OnEndPlay();
    NS::EndDraw(this, &m_tackleGhost);
    NS::EndOverlay(this, m_targetMarker.get());
    NS::EndOverlay(this, m_slamArrow.get());
}

void PlayerActor::OnAppear()
{
    NS::SceneActor::OnAppear();
    if (IsAppeared())
    {
        NS::ShowModel(this, &m_tackleGhost);
        NS::ShowOverlay(this, m_targetMarker.get());
        NS::ShowOverlay(this, m_slamArrow.get());
    }
}

void PlayerActor::OnDisappear() noexcept
{
    NS::SceneActor::OnDisappear();
    NS::HideModel(this, &m_tackleGhost);
    NS::HideOverlay(this, m_targetMarker.get());
    NS::HideOverlay(this, m_slamArrow.get());
}

// 体の形は当たりの部品が持つ世界のカプセルで決まるので、ルートの行列は読まない
NS::SensorVolume PlayerActor::BodySensorVolume(const NS::Matrix&) const noexcept
{
    return NS::SensorVolume::Capsule(m_collider->WorldCapsule());
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
    if (GL::Level::IsMsgCourseStart(msg))
    {
        m_courseStartPosition = Root().Position();
        return true;
    }
    if (GL::Level::IsMsgCourseRestart(msg))
    {
        Restart();
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

void PlayerActor::Restart() noexcept
{
    // 出現位置はカプセル中心の世界座標。コースが始まる前は、シーンに置いた位置のまま
    if (m_courseStartPosition.has_value())
    {
        Root().SetPosition(*m_courseStartPosition);
    }
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

bool PlayerActor::IsDead() const noexcept { return m_health.IsDead(); }

int PlayerActor::Health() const noexcept { return m_health.Current(); }

void PlayerActor::SetDesiredMove(const NS::Vector3& worldDir, float speedScale01) noexcept
{
    m_input->SetDesiredMove(worldDir, speedScale01);
}

float PlayerActor::DesiredSpeedScale() const noexcept { return m_input->DesiredSpeedScale(); }

NS::Vector3 PlayerActor::DesiredDirection() const noexcept { return m_input->DesiredDirection(); }

void PlayerActor::SetClimbMove(float localRight, float localForward) noexcept
{
    m_input->SetClimbMove(localRight, localForward);
}

float PlayerActor::ClimbRight() const noexcept { return m_input->ClimbRight(); }

float PlayerActor::ClimbForward() const noexcept { return m_input->ClimbForward(); }

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

float PlayerActor::RunSpeed() const noexcept { return m_params->m_runSpeed; }

bool PlayerActor::IsBodySlamming() const noexcept
{
    // 状態機械を組む前 (OnInit の前) はまだ預かっていないので nullptr を見る
    return m_states != nullptr && (m_states->IsCurrent<GL::Player::BodySlamPlayerState>() ||
                                   m_states->IsCurrent<GL::Player::BeforeContactPlayerState>());
}

bool PlayerActor::IsInBeforeContact() const noexcept
{
    return m_states != nullptr && m_states->IsCurrent<GL::Player::BeforeContactPlayerState>();
}

float PlayerActor::BodySlamProgress01() const noexcept
{
    if (!IsBodySlamming() || !NS::IsPositive(m_slam.m_distanceTarget))
    {
        return 0.0f;
    }
    return NS::Clamp(m_slam.m_travelled / m_slam.m_distanceTarget, 0.0f, 1.0f);
}

float PlayerActor::BodySlamDistance() const noexcept { return m_params->m_bodySlamDistance; }

bool PlayerActor::IsRebounding() const noexcept
{
    return m_states != nullptr && m_states->IsCurrent<GL::Player::ReboundPlayerState>();
}

bool PlayerActor::IsAirDashing() const noexcept
{
    return m_states != nullptr && m_states->IsCurrent<GL::Player::AirDashPlayerState>();
}

bool PlayerActor::IsDashRebounding() const noexcept
{
    return m_states != nullptr && m_states->IsCurrent<GL::Player::DashReboundPlayerState>();
}

bool PlayerActor::IsReboundLanded() const noexcept
{
    return IsRebounding() && GL::Player::PlayerJudgeLand::Judge(m_body->IsGrounded(), m_body->VerticalVelocity());
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
    // 止めの残りは止めの最後のフレームの終わりで 0 になるので、明けて反動へ移るまでは当たりの状態も読む
    return IsAppeared() && !IsHitStopping() && !IsInTackleHit();
}

bool PlayerActor::IsInTackleHit() const noexcept
{
    return m_states != nullptr && m_states->IsCurrent<GL::Player::TackleHitPlayerState>();
}

bool PlayerActor::IsHeldByTackleHit() const noexcept
{
    return m_resolver->IsHitAwaitingEntry() || IsInTackleHit();
}

NS::HitSensor* PlayerActor::LastTackleTargetSensor() const noexcept
{
    const NS::uint32 targetId = m_resolver->Answer().lastImpact.targetId;
    if (targetId == 0)
    {
        return nullptr;
    }
    NS::SceneActor* target = OwningScene()->Actors().FindActor(NS::ActorRef{targetId});
    if (target == nullptr)
    {
        return nullptr;
    }
    return target->BodySensorClassObj();
}

void PlayerActor::BeginTackleHit() noexcept
{
    const GL::Level::ImpactRecord& impact = m_resolver->Answer().lastImpact;
    NS::HitSensor* target = LastTackleTargetSensor();
    NS::HitReaction* reaction = HitReactionClassObj();
    if (target == nullptr || reaction == nullptr)
    {
        NS_LOG_WARN(Game, "当たりの相手か自機の当たりの演出が無いので、段の束を始めない: 相手 {}", impact.targetId);
        return;
    }
    const std::span<const NS::HitOwnerRun> runs = StartHitReactionRuns(
        GL::Level::TackleSetName("体当たり", impact.tier), impact.surfacePoint, BodySensorClassObj(), target);
    GL::Level::TackleHitDesc hit{.tier = impact.tier, .contact = impact.surfacePoint, .impactDir = impact.impactDir};
    if (impact.broke)
    {
        // 貫通は押し勝っている側なので形を変えず、止めは欄「貫通の止め秒」の長さにする
        OverrideHitStop(impact.hitStopSteps);
        ModelClassObj()->StopDrawShape();
        hit.stopOverride = impact.hitStopSteps;
    }
    for (const NS::HitOwnerRun& run : runs)
    {
        if (run.item.name == "TackleGhost")
        {
            m_ghostRun.Start(run, this);
        }
        if (run.item.name == "HitEffect")
        {
            m_impactEffects->RequestHit();
        }
    }
    (void)GL::Level::SendMsgTackleHit(target, BodySensorClassObj(), hit);
}

void PlayerActor::ReleaseTackleHit() noexcept
{
    const GL::Level::ImpactRecord& impact = m_resolver->Answer().lastImpact;
    NS::HitSensor* target = LastTackleTargetSensor();
    NS::HitReaction* reaction = HitReactionClassObj();
    if (target != nullptr && reaction != nullptr)
    {
        // 束が足した持ち主へ渡す項目は、走っている項目の後ろに並ぶ
        const std::size_t first = reaction->OwnerRuns().size();
        (void)StartHitReaction(
            GL::Level::TackleSetName("明け", impact.tier), impact.surfacePoint, BodySensorClassObj(), target);
        // 紫で上がった威力を当たった結果で見せる遅さなので、赤で放した当たりには出さない
        if (impact.gradualRelease)
        {
            m_slowedWorld = StartHitReaction(
                GL::Level::TackleSetName("明け", impact.tier, "・紫"), impact.surfacePoint, BodySensorClassObj(), target);
        }
        for (const NS::HitOwnerRun& run : reaction->OwnerRunsSince(first))
        {
            if (run.item.name == "FlightEffect")
            {
                m_impactEffects->RequestFlight();
            }
        }
    }
    m_resolver->Release();
}

void PlayerActor::StartReboundReaction() noexcept
{
    NS::HitSensor* target = LastTackleTargetSensor();
    if (target == nullptr)
    {
        return;
    }
    (void)StartHitReaction("反動", m_resolver->Answer().lastImpact.surfacePoint, BodySensorClassObj(), target);
}

void PlayerActor::StopTackleHitEffects() noexcept
{
    if (NS::Model* model = ModelClassObj())
    {
        model->StopDrawShake();
        model->StopDrawTremor();
        model->StopDrawShape();
    }
    if (NS::HitReaction* reaction = HitReactionClassObj())
    {
        reaction->StopGroundWave(this);
        reaction->StopDistortionRing(this);
    }
    m_ghostRun.Stop(&m_tackleGhost);
    // 紫の明けの遅い世界は束より長く続く。打ち切る当たりが遅くした世界を普段の速さへ戻す
    if (m_slowedWorld)
    {
        m_slowedWorld = false;
        OwningScene()->SetWorldSpeed(1.0f);
    }
}

GL::Player::PlayerMotion PlayerActor::Motion() const noexcept
{
    return GL::Player::PlayerMotion{
        .bodySlamming = IsBodySlamming(),
        .beforeContact = IsInBeforeContact(),
        .rebounding = IsRebounding(),
        .reboundLanded = IsReboundLanded(),
        .skidding = IsSkidding(),
        .curled = IsCurled(),
        .canMoveBody = CanMoveBody(),
        .airDashing = IsAirDashing(),
        .dashRebounding = IsDashRebounding(),
        .velocity = m_body->Velocity(),
    };
}

void PlayerActor::ResetState() noexcept
{
    // 状態機械を戻す前に読む。自機を止めていない時は、前の当たりの白・振動・カメラの返りの薄れを残す
    const bool heldByHit = IsHeldByTackleHit();
    m_body->SetVelocity(NS::Vector3{0.0f, 0.0f, 0.0f});
    m_input->ResetMovementInput();
    m_prevJumpHeld = false;
    m_jumpsRemaining = 1;
    m_coyoteTimer = 0.0f;
    m_bufferTimer = 0.0f;
    m_airControlReturning = false;
    m_airControlReturnElapsed = 0.0f;
    m_body->SetGrounded(false);
    m_ledgeTopY = 0.0f;
    m_ledgeFaceNormal = NS::Vector3{0.0f, 0.0f, 0.0f};
    m_ledgeMantleTimer = 0.0f;
    m_facingDir = NS::Vector3{0.0f, 0.0f, 0.0f};
    m_lastMoveDistance = 0.0f;
    m_request = BodySlamRequest{};
    // 押したまま出直すと、次の押しのフレームから溜め直す。残すと、出直した後に放したフレームで前の溜めの突進が出る
    CancelCharge();
    m_chargeEffects->Clear(this);
    m_slam = GL::Player::BodySlamStatus{};
    m_rebound.Reset();
    m_skid = GL::Player::SkidStatus{};
    // 押しの控えは残す。押したまま出直した歩を、押した瞬間と読まない
    m_airDash.Reset();
    // 当たりの形だけを立ち姿へ戻し、根は動かさない。出直しは根を出現位置へ置いてから呼ぶので、
    // 丸まりを解く時のように根を上げると出現位置より半長ぶん高く湧いた
    m_collider->SetSphereShape(false);
    m_appearance->Uncurl(m_model.get());
    m_states->Reset();
    // 止めの最中か明けを待つ間のやり直しで、出現位置で明けて弾かれないよう、止めと当たりの状態を捨てる。
    // StateMachineOf::Reset は OnExit を呼ばないので、当たりの演出もここで止める
    OverrideHitStop(0);
    m_resolver->CancelImpact();
    StopTackleHitEffects();
    if (heldByHit && HitReactionClassObj() != nullptr)
    {
        HitReactionClassObj()->Stop(this);
    }
    // 当たる直前の動作が止めた自機以外の世界を、出現位置の世界へ持ち越さない
    ActionTimelineClassObj()->Stop();
    m_beforeContact = BeforeContactEntry{};
    OwningScene()->HoldOthers(this, 0);
    // 潰れたままの描く形から出現位置の形へ補間されないよう、前のフレームの倍率ごと揃える
    m_appearance->ResetDrawScale(m_model.get());
}

void PlayerActor::DropPresses() noexcept
{
    m_input->ConsumePressed();
    m_prevJumpHeld = m_input->JumpHeld();
}

void PlayerActor::PrepareStateStep()
{
    const bool locomotion = GL::Player::PlayerJudgeLocomotion::Judge(GL::Player::PlayerJudgeLocomotion::StateFlags{
        .idle = m_states->IsCurrent<GL::Player::IdlePlayerState>(),
        .walk = m_states->IsCurrent<GL::Player::WalkPlayerState>(),
        .fall = m_states->IsCurrent<GL::Player::FallPlayerState>(),
        .rebound = m_states->IsCurrent<GL::Player::ReboundPlayerState>(),
    });
    if (CanStartAirDash() && BeginAirDash())
    {
        return;
    }
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

