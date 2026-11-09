#include "Game/PlayerActor.h"

#include "Game/Level/GameScene.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/ReboundPreview.h"
#include "Game/Level/SensorKinds.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/PlayerSounds.h"
#include "Game/Player/States/AirDashPlayerState.h"
#include "Game/Player/States/BrakePlayerState.h"
#include "Game/Player/States/DashReboundPlayerState.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/LedgeClimbingPlayerState.h"
#include "Game/Player/States/LedgeHangingPlayerState.h"
#include "Game/Player/States/WalkPlayerState.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/CoreObject/Action/ActorAction.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Asset/AnimClip.h"
#include "NSlib/CoreObject/ClassObjs/Animation.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/EffectObj.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/CoreObject/ClassObjs/Shadow.h"
#include "NSlib/CoreObject/ClassObjs/Sound.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string_view>
#include <variant>

namespace
{
    // 自機の動きの動作の名前。PlayerActor.actor の動作の名前と合わせる
    constexpr std::string_view k_WaitAction = "Wait";
    constexpr std::string_view k_WalkAction = "Walk";
    constexpr std::string_view k_RunAction = "Run";
    constexpr std::string_view k_JumpAction = "Jump";
    constexpr std::string_view k_FallAction = "Fall";
    constexpr std::string_view k_LedgeHangAction = "LedgeHang";

    // actions の name の動作の、最初のアニメーションの項目のクリップ。動作か項目が無ければ nullptr
    const NS::AnimClip* ActionClip(const NS::ActorAction* actions, std::string_view name) noexcept
    {
        if (actions == nullptr)
        {
            return nullptr;
        }
        const NS::ActorActionInfo* action = actions->FindAction(name);
        if (action == nullptr)
        {
            return nullptr;
        }
        for (const NS::ActorActionEvent& event : action->events)
        {
            if (const NS::ActionAnimInfo* anim = std::get_if<NS::ActionAnimInfo>(&event.item))
            {
                return anim->clip;
            }
        }
        return nullptr;
    }

    // name の動作のアニメーションの項目に、クリップの道を書いてある場合 true。読めるかは見ない
    bool HasActionClip(const NS::ActorAction* actions, std::string_view name) noexcept
    {
        const NS::AnimClip* clip = ActionClip(actions, name);
        return clip != nullptr && !clip->Path().empty();
    }

    // name の動作のアニメーションの項目のクリップを読めていて、流せる場合 true
    bool CanPlayAction(const NS::ActorAction* actions, std::string_view name) noexcept
    {
        const NS::AnimClip* clip = ActionClip(actions, name);
        return clip != nullptr && clip->Clip() != nullptr;
    }
} // namespace

NS_REFLECT_BEGIN_IMPL(PlayerActor, NS::SceneActor)
NS_CLASSOBJ(m_appearance, "Appearance")
NS_CLASSOBJ(m_body, "Movement")
NS_CLASSOBJ(m_collider, "Collider")
NS_CLASSOBJ(m_input, "Input")
NS_CLASSOBJ(m_params, "Params")
NS_CLASSOBJ(m_reboundPreview, "ReboundPreview")
NS_CLASSOBJ(m_playerSounds, "PlayerSounds")
NS_REFLECT_END_IMPL()

NS_PLACEABLE(PlayerActor, "自機")

PlayerActor::PlayerActor() noexcept = default;

PlayerActor::~PlayerActor() = default;
void PlayerActor::OnInit()
{
    CreateClassObj(m_hitSensors);
    m_bodyHitSensor =
        m_hitSensors->AddFollow(this, "Body", GL::Level::SensorKind::PlayerBody, [this](const NS::Matrix& rootWorld) {
            return BodySensorVolume(rootWorld);
        });
    CreateClassObj(m_model);
    NS::InitDraw(this, m_model.get(), &Root());
    CreateClassObj(m_appearance);
    CreateClassObj(m_body);
    // シーンは OnInit の前に決まっている。当たりは作る時に地形と根を受け取り、持ち主を持たない
    CreateClassObj(m_collider, GetPhysicsScene(), &Root());
    CreateClassObj(m_input);
    CreateClassObj(m_params);
    CreateClassObj(m_shadow);
    NS::InitDraw(this, m_shadow.get(), &Root(), OwningScene());
    m_ghost = CreateChildActor<GL::Level::TackleGhost>();
    CreateClassObj(m_hitReaction);
    CreateClassObj(m_actorAction);
    CreateClassObj(m_reboundPreview);
    NS::InitOverlay(this, m_reboundPreview.get());
    CreateClassObj(m_effect);
    CreateClassObj(m_sound);
    CreateClassObj(m_playerSounds);
    // 着地は出来事の受け手が頼むだけで、鳴らすのは閃光と同じ VisualStep。跳ぶ音は動作 Jump の項目が鳴らす
    m_playerEvents.onJump.Subscribe([this] { m_playerSounds->AllowLand(); });
    m_body->Events().onGroundExit.Subscribe([this] { m_playerSounds->AllowLand(); });
    m_body->Events().onGroundEnter.Subscribe([this] { m_playerSounds->RequestLand(); });
    // 部品を全部作った後に組む。並べた型が移れる状態の全部になる。先頭の立ちへは最初の更新の初めに入る
    (void)BuildStateMachine<PlayerActor,
                            GL::Player::IdlePlayerState,
                            GL::Player::WalkPlayerState,
                            GL::Player::FallPlayerState,
                            GL::Player::LedgeHangingPlayerState,
                            GL::Player::LedgeClimbingPlayerState,
                            GL::Player::BrakePlayerState,
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
    state.rebound = GL::Level::FollowReboundDesc{
        .widenView = IsAirDashing() || IsDashRebounding(),
    };
    // 帯の縦は表で丸めた向きから取るので、同じ当て方なら毎回同じ画面になる
    if (IsDashRebounding())
    {
        state.rebound.rebounding = true;
        state.rebound.verticalLean = std::clamp(m_airDash.LastRebound().shifted.y, -1.0f, 1.0f);
    }
    return state;
}

void PlayerActor::WriteFollowState(GL::Level::ThirdPersonFollow* vcam) const
{
    NS_ASSERT(Game, vcam != nullptr, "PlayerActor::WriteFollowState: 書く先の追従カメラが無い");
    const GL::Player::CameraTargetState state = GetCameraTargetState();
    vcam->SetFollowMotion(state.grounded, state.velocity);
    vcam->SetTargetHeightOffset(state.heightOffset);
    if (!vcam->SetFollowRebound(state.rebound))
    {
        NS_LOG_WARN(Game, "反動の縦の向きが壊れていて、反動をカメラへ渡さなかった: {}", state.rebound.verticalLean);
    }
}

void PlayerActor::UpdateAnimation()
{
    NS::Animation* animation = AnimationClassObj();
    const NS::ActorAction* actions = ActorActionClassObj();
    if (animation == nullptr || actions == nullptr)
    {
        return;
    }

    const float lateralSpeed = m_body->LateralVelocity().Length();
    const std::string_view chosen = ChooseMovementAction(lateralSpeed);
    // 同じ動作を始め直すとクリップの時刻が頭へ戻るので、名前が変わった時だけ始める
    if (!chosen.empty() && chosen != actions->CurrentName() && actions->FindAction(chosen) != nullptr)
    {
        (void)StartAction(chosen);
    }

    animation->SetSpeed(ChoosePlaybackSpeed(actions->CurrentName(), lateralSpeed));
}

void PlayerActor::Update(bool slamHeld)
{
    m_input->SetSlamHeld(slamHeld);
    m_inputInjected = true;
    NS::SceneActor::Update();
    m_inputInjected = false;
}

void PlayerActor::DecideStep()
{
    // 固定フレームごとに 1 回、前に読んだ後の押しをここで拾う
    if (!m_inputInjected)
    {
        m_input->Update(this);
    }
    // 止めの間は、やり直しの押しを受け付けない
    const bool hitAtStepStart = IsHitStopping();
    // SlamHeld のボタンはエアダッシュに使う。押した瞬間に出す
    const bool pressHeld = m_input->SlamHeld();
    m_airDash.m_pressed = pressHeld && !m_airDash.m_prevPressHeld;
    m_airDash.m_prevPressHeld = pressHeld;
    ObserveStick();
    ExtendReboundStop();
    // ヒットストップの間は押しを下ろさないので、その間の押しは明けのフレームまで残る
    if (m_input->RespawnPressed() && !hitAtStepStart)
    {
        if (GL::Level::GameScene* game = NS::SceneCast<GL::Level::GameScene>(OwningScene()))
        {
            game->RequestRespawnActors();
        }
    }
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
    SkipStateStep();
    return false;
}

void PlayerActor::AfterStateStep()
{
    if (m_stateStepPrepared)
    {
        FinishStateStep();
    }
}

void PlayerActor::ChangeToWalkOrFall() noexcept
{
    if (m_body->IsGrounded())
    {
        (void)m_states->Change<GL::Player::WalkPlayerState>();
    }
    else
    {
        (void)m_states->Change<GL::Player::FallPlayerState>();
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
    UpdateAnimation();
#if !defined(NS_SHIPPING)
    UpdateReboundPreview();
#endif
    const GL::Player::PlayerMotion motion = Motion();
    m_appearance->Update(m_model.get(), NS::GetFrameDelta(this), motion);
    // 音を重ねない間隔なので、ヒットストップでも伸び縮みしない実時間の秒で数える
    m_playerSounds->Update(this, NS::OS::FrameTimer::FixedDelta());
    DrawTerrainRebound();
}

std::string_view PlayerActor::ChooseMovementAction(float lateralSpeed) const noexcept
{
    // エアダッシュ・反動・よじ登りの状態は、動きの動作を始めない。エアダッシュは入る時に自分の動作を始める
    if (m_states->IsCurrent<GL::Player::AirDashPlayerState>() ||
        m_states->IsCurrent<GL::Player::DashReboundPlayerState>() ||
        m_states->IsCurrent<GL::Player::LedgeClimbingPlayerState>())
    {
        return {};
    }
    const NS::ActorAction* actions = ActorActionClassObj();
    // 跳んだ所 (LaunchJump) で始めた Jump は、上がっている間は残す。跳ばずに上向きに飛ぶ時は Jump を始めない
    if (actions->CurrentName() == k_JumpAction && m_body->VerticalVelocity() > 0.0f)
    {
        return {};
    }
    // 選ぶのはクリップの道を書いた動作。道はあるが読めないクリップの動作は、立ちの動作で代える
    std::string_view chosen = k_WaitAction;
    if (m_states->IsCurrent<GL::Player::LedgeHangingPlayerState>())
    {
        if (HasActionClip(actions, k_LedgeHangAction))
        {
            chosen = k_LedgeHangAction;
        }
    }
    else if (!m_body->IsGrounded())
    {
        if (HasActionClip(actions, k_FallAction))
        {
            chosen = k_FallAction;
        }
    }
    else if (!NS::IsNearlyZero(lateralSpeed))
    {
        const float maxSpeed = MaxSpeed();
        if (HasActionClip(actions, k_WalkAction))
        {
            chosen = k_WalkAction;
        }
        if (maxSpeed > 0.0f && lateralSpeed >= maxSpeed * m_appearance->RunBlendRatio() &&
            HasActionClip(actions, k_RunAction))
        {
            chosen = k_RunAction;
        }
    }
    if (!CanPlayAction(actions, chosen))
    {
        return k_WaitAction;
    }
    return chosen;
}

void PlayerActor::StartJumpAction() noexcept
{
    (void)StartAction(k_JumpAction);
}

float PlayerActor::ChoosePlaybackSpeed(std::string_view action, float lateralSpeed) const noexcept
{
    if (action != k_WalkAction && action != k_RunAction)
    {
        return 1.0f;
    }

    const float maxSpeed = MaxSpeed();
    if (maxSpeed <= 0.0f)
    {
        return 1.0f;
    }
    return std::max(m_appearance->MinPlaybackSpeed(), lateralSpeed / maxSpeed);
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
    m_playerSounds->Preload(this);
}

void PlayerActor::OnEndPlay()
{
    // 戻す物はやり直しと同じ組。当たりの止めと状態と演出も捨て、止めが次のプレイへ残らない
    ResetState();
    NS::SceneActor::OnEndPlay();
    NS::EndOverlay(this, m_reboundPreview.get());
}

void PlayerActor::OnAppear()
{
    NS::SceneActor::OnAppear();
    if (IsAppeared())
    {
        NS::ShowOverlay(this, m_reboundPreview.get());
    }
}

void PlayerActor::OnDisappear() noexcept
{
    NS::SceneActor::OnDisappear();
    NS::HideOverlay(this, m_reboundPreview.get());
}

// 体の形は当たりの部品が持つ世界のカプセルで決まるので、ルートの行列は読まない
NS::SensorVolume PlayerActor::BodySensorVolume(const NS::Matrix&) const noexcept
{
    return NS::SensorVolume::Capsule(m_collider->WorldCapsule());
}

void PlayerActor::InitAfterPlacement()
{
    ResetHealth();
}

bool PlayerActor::ReceiveMsg(const NS::Message& msg, NS::HitSensor* sender, NS::HitSensor* receiver)
{
    (void)sender;
    (void)receiver;
    if (NS::IsMsg<GL::Level::MsgInstantDeath>(msg))
    {
        // 消えた後は VisualStep が呼ばれないので、その場で鳴らす。重なった範囲から同じフレームに何度も届いても 1 回だけ
        if (!IsDead())
        {
            m_playerSounds->PlayFall(this);
        }
        Die();
        if (GL::Level::GameScene* game = NS::SceneCast<GL::Level::GameScene>(OwningScene()))
        {
            game->NotifyPlayerDead();
        }
        return true;
    }
    if (const GL::Level::MsgGoal* goal = NS::MsgCast<GL::Level::MsgGoal>(msg))
    {
        if (GL::Level::GameScene* game = NS::SceneCast<GL::Level::GameScene>(OwningScene()))
        {
            // ゴールの知らせは触れている間ずっと届くので、クリアを始めた 1 回だけ鳴らす
            if (game->NotifyGoal(goal->FadeOutSeconds(), goal->FadeInSeconds()))
            {
                m_playerSounds->PlayGoal(this);
            }
        }
        return true;
    }
    if (NS::IsMsg<GL::Level::MsgCourseStart>(msg))
    {
        m_courseStartPosition = Root().Position();
        return true;
    }
    if (NS::IsMsg<GL::Level::MsgCourseRestart>(msg))
    {
        Restart();
        return true;
    }
    if (const GL::Level::MsgInputLock* lock = NS::MsgCast<GL::Level::MsgInputLock>(msg))
    {
        m_input->SetLocked(lock->Locked());
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

bool PlayerActor::IsDead() const noexcept
{
    return m_health.IsDead();
}

int PlayerActor::Health() const noexcept
{
    return m_health.Current();
}

float PlayerActor::MaxSpeed() const noexcept
{
    return std::max(m_params->m_runSpeed, 0.0f);
}

float PlayerActor::RunSpeed() const noexcept
{
    return m_params->m_runSpeed;
}

bool PlayerActor::IsAirDashing() const noexcept
{
    // 状態機械を組む前 (OnInit の前) はまだ預かっていないので nullptr を見る
    return m_states != nullptr && m_states->IsCurrent<GL::Player::AirDashPlayerState>();
}

bool PlayerActor::IsDashRebounding() const noexcept
{
    return m_states != nullptr && m_states->IsCurrent<GL::Player::DashReboundPlayerState>();
}

bool PlayerActor::CanMoveBody() const noexcept
{
    return IsAppeared() && !IsHitStopping();
}

void PlayerActor::StopTackleHitEffects() noexcept
{
    if (NS::HitReaction* reaction = HitReactionClassObj())
    {
        // 自機が預ける走る物は、当たりの束が描く形へ書く見た目だけなので、全部を止める
        reaction->StopRuns(NS::HitReactionSelf{.actor = this, .model = ModelClassObj()});
    }
    if (GL::Level::HitScreenDirector* screen = GL::Level::HitScreenOf(this))
    {
        screen->StopGroundWave(this);
        screen->StopDistortionRing(this);
    }
    m_ghost->Stop();
}

GL::Player::PlayerMotion PlayerActor::Motion() const noexcept
{
    return GL::Player::PlayerMotion{
        .curled = IsCurled(),
        .canMoveBody = CanMoveBody(),
        .airDashing = IsAirDashing(),
        .dashRebounding = IsDashRebounding(),
        .velocity = m_body->Velocity(),
    };
}

void PlayerActor::ResetState() noexcept
{
    m_body->SetVelocity(NS::Vector3{0.0f, 0.0f, 0.0f});
    m_input->ResetMovementInput();
    m_prevJumpHeld = false;
    m_jumpsRemaining = 1;
    m_coyoteTimer = 0;
    m_bufferTimer = 0;
    m_airControlReturning = false;
    m_airControlReturnElapsed = 0;
    m_body->SetGrounded(false);
    m_ledgeTopY = 0.0f;
    m_ledgeFaceNormal = NS::Vector3{0.0f, 0.0f, 0.0f};
    m_ledgeMantleTimer = 0;
    m_facingDir = NS::Vector3{0.0f, 0.0f, 0.0f};
    m_lastMoveDistance = 0.0f;
    // 押しの控えは残す。押したまま出直した歩を、押した瞬間と読まない
    m_airDash.Reset();
    m_reboundPreview->Hide();
    // 当たりの形だけを立ち姿へ戻し、根は動かさない。出直しは根を出現位置へ置いてから呼ぶので、
    // 丸まりを解く時のように根を上げると出現位置より半長ぶん高く湧いた
    m_collider->SetSphereShape(false);
    m_appearance->Uncurl(m_model.get());
    m_states->Reset();
    // 止めの最中のやり直しで、出現位置で明けて弾かれないよう、止めを捨てる。
    // StateMachineOf::Reset は OnExit を呼ばないので、当たりの演出もここで止める
    OverrideHitStop(0);
    StopTackleHitEffects();
    // 動作が止めた自機以外の世界を、出現位置の世界へ持ち越さない
    StopAction();
    OwningScene()->RequestStopScene(this, 0);
    // 潰れたままの描く形から出現位置の形へ補間されないよう、前のフレームの倍率ごと揃える
    m_appearance->ResetDrawScale(m_model.get());
    // 状態機械は立ちへ戻したので、立ちのクリップを前の姿勢から混ぜずに頭から流す。
    // やり直しは StepGame からアクターの Update より後に届くので、外したままにすると、次の VisualStep
    // までの描画が骨格の初期の姿勢になる
    if (NS::Animation* animation = AnimationClassObj())
    {
        animation->Play(nullptr);
        // OnEndPlay
        // からも呼ばれるので、動作は始めずクリップだけを流す。立ちの動作の効果音や振動を、終わる時に起こさない。 次の
        // VisualStep が立ちの動作を始め、同じクリップなので頭から流し直さない
        if (CanPlayAction(ActorActionClassObj(), k_WaitAction))
        {
            animation->Play(ActionClip(ActorActionClassObj(), k_WaitAction)->Clip());
        }
    }
    // 次に足場を離れるまで着地の音を鳴らさない。出現位置へ着いた最初の着地で鳴らさないため
    m_playerSounds->Reset();
}

void PlayerActor::DropPresses() noexcept
{
    m_input->ConsumePressed();
    m_prevJumpHeld = m_input->JumpHeld();
}

void PlayerActor::PrepareStateStep()
{
    // ここで始める動作は宙でも出せる。出現した後に一度も接地しないまま出しても、次の着地で着地の音を鳴らす
    if (CanStartAirDash() && BeginAirDash())
    {
        m_playerSounds->AllowLand();
    }
}

void PlayerActor::FinishStateStep()
{
    UncurlWhenSettled();
    m_prevJumpHeld = m_input->JumpHeld();
    m_input->ConsumePressed();
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
