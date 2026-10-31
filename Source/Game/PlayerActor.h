#pragma once

#include "Game/Level/Health.h"
#include "Game/Level/ImpactInputJudge.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/SensorKinds.h"
#include "Game/Level/SlamAim.h"
#include "Game/Level/TackleGhost.h"
#include "Game/Player/CameraTargetState.h"
#include "Game/Player/PlayerEvents.h"
#include "Game/Player/PlayerStatus.h"
#include "Game/Player/ReboundArc.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/Sphere.h"
#include "NSlib/CoreObject/IGet/IGetCameraTarget.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/StateMachineOf.h"

#include "NSlib/Core/Types.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NS
{
    namespace Anim
    {
        struct AnimClip;
    }
    class AnimClip;
    class Body;
    class Collider;
    class ActorList;
    class PlayerInput;
} // namespace NS

namespace GL
{
    namespace Player
    {
        class PlayerParams;
        class PlayerClips;
        class PlayerAppearance;
        class ImpactEffects;
        class PlayerSounds;
    } // namespace Player
} // namespace GL

namespace GL
{
    namespace Level
    {
        class ImpactResolver;
        class TargetMarker;
        class SlamArrow;
        struct BodySlamMotion;
        struct ImpactAnswer;
        struct ImpactRecord;
        struct ImpactSelf;
    } // namespace Level
} // namespace GL

//! 固定の部品を OnInit で組むプレイヤー
//! 衝突の観測・裁定を身体の移動より先に行う。死とゴールは Scene の CourseDirector に通知する
class PlayerActor : public NS::SceneActor, public NS::IGetCameraTarget
{
public:
    PlayerActor() noexcept;
    ~PlayerActor() override;

    NS_REFLECT_DECLARE()
    // =========================================================================
    // コンポーネント・基本情報の参照
    // =========================================================================
    [[nodiscard]] NS::StateMachineOf<PlayerActor>& States() noexcept { return *m_states; }
    [[nodiscard]] const NS::StateMachineOf<PlayerActor>& States() const noexcept { return *m_states; }
    [[nodiscard]] NS::PlayerInput& Input() noexcept { return *m_input; }
    [[nodiscard]] const NS::PlayerInput& Input() const noexcept { return *m_input; }
    [[nodiscard]] NS::Body& Body() noexcept { return *m_body; }
    [[nodiscard]] const NS::Body& Body() const noexcept { return *m_body; }
    [[nodiscard]] NS::Collider& Collider() noexcept { return *m_collider; }
    [[nodiscard]] const NS::Collider& Collider() const noexcept { return *m_collider; }
    [[nodiscard]] GL::Player::PlayerParams& Params() noexcept { return *m_params; }
    [[nodiscard]] const GL::Player::PlayerParams& Params() const noexcept { return *m_params; }
    [[nodiscard]] GL::Player::PlayerClips& Clips() noexcept { return *m_clips; }
    [[nodiscard]] const GL::Player::PlayerClips& Clips() const noexcept { return *m_clips; }
    [[nodiscard]] const GL::Player::PlayerSounds& Sounds() const noexcept { return *m_playerSounds; }
    // =========================================================================
    // チャージ・照準・演出サブシステムの参照
    // =========================================================================
    [[nodiscard]] const GL::Level::ImpactInputJudge& ChargeJudge() const noexcept;
    //! 保持中の狙いの線を返す。入力方向ではなく、効果を掛ける前のカメラ方向を使う
    //! 押していない、カメラ管理なし、無効な方向では false で outLine を変更しない
    [[nodiscard]] bool TryGetAimLine(GL::Level::AimLine& outLine) const noexcept;
    //! 揺れを足す前の線で狙う相手を返す。相手が無い場合は false で outTarget を変更しない
    [[nodiscard]] bool TryGetAimTarget(GL::Level::SlamLineTarget& outTarget) const noexcept;
    //! 溜めすぎの揺れを足した線で、最初に触れる相手を返す
    //! 枠と縦速度は TryGetAimTarget の相手を使う。相手が無い場合は false で outTarget を変更しない
    [[nodiscard]] bool TryGetLineTarget(GL::Level::SlamLineTarget& outTarget) const noexcept;
    //! 紫の揺れの位相 (ラジアン)。紫になりきった瞬間に π/2 を通り、紫でない間は 0
    [[nodiscard]] float ChargeSwayPhase() const noexcept { return m_charge.SwayPhase(); }
    //! 狙いの線の右を正にした、相手の所 (相手がいなければ規定距離) での紫の揺れの横のずれ (m)。紫でない間は 0
    [[nodiscard]] float ChargeSwayOffset() const noexcept { return m_charge.SwayOffset(); }
    //! このフレームに紫の揺れが端 (位相 π/2 + nπ) を通ったか。紫に入ったフレームは数えない
    [[nodiscard]] bool ChargeSwayReachedEdge() const noexcept { return m_charge.SwayReachedEdge(); }
    //! 構えの縦倍率を返す。非保持時は 1
    [[nodiscard]] float StanceHeight() const noexcept;
    [[nodiscard]] GL::Level::ImpactResolver& Resolver() noexcept { return *m_resolver; }
    [[nodiscard]] GL::Player::PlayerAppearance& Appearance() noexcept { return *m_appearance; }
    [[nodiscard]] GL::Level::TargetMarker& TargetIndicator() noexcept { return *m_targetMarker; }
    [[nodiscard]] GL::Level::SlamArrow& SlamIndicator() noexcept { return *m_slamArrow; }
    [[nodiscard]] GL::Player::ImpactEffects& ImpactVisuals() noexcept { return *m_impactEffects; }
    [[nodiscard]] GL::Level::TackleGhost* Ghost() noexcept { return NS::Cast<GL::Level::TackleGhost>(ChildActor()); }
    [[nodiscard]] const GL::Level::TackleGhost* Ghost() const noexcept
    {
        return NS::Cast<GL::Level::TackleGhost>(ChildActor());
    }

    // =========================================================================
    // 更新・アニメーション・移動入力・ジャンプ状態
    // =========================================================================
    using NS::SceneActor::Update;
    //! 機器を読まずに chargeHeld を差し込み、1 固定ステップ進める。ほかの入力は部品の Set〜 で書いた値のまま
    void Update(bool chargeHeld);
    //! 状態と水平の速さから再生するクリップと速度を選び、同居する Animation へ渡す
    void UpdateAnimation();
    void OnEndPlay() override;

    //! 世界座標系の目標移動方向と速度スケール (0..1 に丸める) を渡す
    void SetDesiredMove(const NS::Vector3& worldDir, float speedScale01) noexcept;
    //! 直近に渡された目標速度スケール 0..1
    [[nodiscard]] float DesiredSpeedScale() const noexcept;
    //! 直近に渡された世界座標系の目標移動方向。長さは入力の強さのままで正規化されていない
    [[nodiscard]] NS::Vector3 DesiredDirection() const noexcept;
    //! 掴まり中の生ローカル入力 (-1..1)。SetDesiredMove とは別に持つ
    void SetClimbMove(float localRight, float localForward) noexcept;
    [[nodiscard]] float ClimbRight() const noexcept;
    [[nodiscard]] float ClimbForward() const noexcept;
    //! ジャンプの押下を一度記録する。更新の終わりに消費する
    void SetJumpPressed() noexcept;
    //! 手放しの押下を一度記録する。更新の終わりに消費する
    void SetReleaseLedgePressed() noexcept;
    //! ジャンプボタンの長押し状態を渡す。上昇中に離すと縦速度を縮める
    void SetJumpHeld(bool held) noexcept;

    [[nodiscard]] int JumpsRemaining() const noexcept { return m_jumpsRemaining; }
    //! コヨーテ猶予の残り秒。0 以下なら宙からは跳べない
    [[nodiscard]] float CoyoteRemaining() const noexcept { return m_coyoteTimer; }
    //! 走行速度に溜め中の倍率を掛けた最高速度 (m/s、下限 0)
    [[nodiscard]] float MaxSpeed() const noexcept;
    [[nodiscard]] float RunSpeed() const noexcept;
    //! 先に根を出現位置へ置いて呼ぶ。根は動かさず、移動・溜め・命中・丸まり・描画倍率を初期状態へ戻す
    void ResetState() noexcept;
    //! 着地でジャンプ回数を戻し、接地中はコヨーテ猶予と突進の使用済みを戻す
    void SyncGroundState() noexcept;
    [[nodiscard]] GL::Player::PlayerEvents& PlayerEventsRef() noexcept { return m_playerEvents; }

    // =========================================================================
    // 体当たり（突進）・反動・エアダッシュ・丸まりの状態と操作
    // =========================================================================
    //! 突進または当たる直前の状態に居るか
    [[nodiscard]] bool IsBodySlamming() const noexcept;
    [[nodiscard]] bool IsInBeforeContact() const noexcept;
    //! 予測した相手の段の当たる直前の動作を、残りの歩に合うフレームから始める
    void BeginBeforeContact() noexcept;
    //! 検知で出る時は何もせず、予測が外れて出る時は形と自機以外の止めと返りを戻す
    void EndBeforeContact() noexcept;
    //! 突進の進み具合 0..1。突進中でなければ 0
    [[nodiscard]] float BodySlamProgress01() const noexcept;
    //! 発動時の溜め量 0..1
    [[nodiscard]] float BodySlamCharge01() const noexcept { return m_slam.Charge01(); }
    //! 発動時の溜めすぎの深さ 0..1。赤で放した突進と通常突進は 0
    [[nodiscard]] float BodySlamOvercharge01() const noexcept { return m_slam.Overcharge01(); }
    //! 溜めた突進を終える水平の距離 (m)
    [[nodiscard]] float BodySlamDistance() const noexcept;
    //! 最後に出した突進の正規化済みの水平の向き。突進を出す前と ResetState の後はゼロ
    [[nodiscard]] NS::Vector3 BodySlamDirection() const noexcept { return m_slam.Direction(); }
    [[nodiscard]] bool IsRebounding() const noexcept;
    [[nodiscard]] bool IsAirDashing() const noexcept;
    [[nodiscard]] bool IsDashRebounding() const noexcept;
    //! 今出せるエアダッシュの回数。接地の間と、反動の補正の段に入った時に 1 に戻る
    [[nodiscard]] int AirDashesRemaining() const noexcept { return m_airDash.Remaining(); }
    [[nodiscard]] const GL::Player::AirDashStatus& AirDash() const noexcept { return m_airDash; }
    //! エアダッシュの動作 ("AirDash") を今の状態に結んで始める。タイムラインが無いか開始できなければ false
    [[nodiscard]] bool StartAirDashAction() noexcept;
    //! 重力を掛けずに、向き × 控えたダッシュの速さを身体に書く
    void KeepAirDashSpeed() noexcept;
    //! 当たらずに終わったエアダッシュの勢いを残す
    void KeepAirDashExitSpeed() noexcept;
    //! 接地していれば歩きへ、空中なら落下へ移す
    void ChangeToWalkOrFall() noexcept;
    //! 地形の反動の 1 フレームを進める
    void UpdateDashRebound(float dt) noexcept;
    [[nodiscard]] bool IsReboundLanded() const noexcept;
    [[nodiscard]] bool IsSkidding() const noexcept;
    //! 外れで着地後に滑って止まるか。滑るフレーム数が 0 なら false
    [[nodiscard]] bool SkidsOnLanding() const noexcept;
    //! 出ていて、当たりの止めの外で、当たりの状態に居ないか
    [[nodiscard]] bool CanMoveBody() const noexcept;
    [[nodiscard]] bool IsInTackleHit() const noexcept;
    //! 段の束を触れた点と 2 つのセンサーで始め、相手へ当たったを知らせる
    void BeginTackleHit() noexcept;
    //! 束「反動」を最後の当たりの触れた点と 2 つのセンサーで始める
    void StartReboundReaction() noexcept;
    //! 最後に始めた反動の正規化済みの水平の向き。反動を始める前と ResetState の後はゼロ
    [[nodiscard]] NS::Vector3 ReboundDirection() const noexcept { return m_rebound.Direction(); }
    //! 最後に出した突進の角速度 (度/秒)。突進を出す前と ResetState 後は 0
    [[nodiscard]] float BodySlamSpinSpeed() const noexcept { return m_slam.SpinSpeed(); }
    //! 最後に始めた反動の角速度 (度/秒)。開始前と ResetState 後は 0
    [[nodiscard]] float ReboundSpinSpeed() const noexcept { return m_rebound.SpinSpeed(); }
    //! 外れの反動の時だけ値を持つ
    [[nodiscard]] const std::optional<GL::Player::MissTumble>& ReboundMissTumble() const noexcept
    {
        return m_rebound.Tumble();
    }
    //! 反動を始めた回数。ResetState では戻さない
    [[nodiscard]] NS::uint32 ReboundCount() const noexcept { return m_rebound.Count(); }
    //! こすって止まる間の、着いた水平の速さに掛けている倍率 (0..1)。こすり状態でない間は 1
    [[nodiscard]] float SkidSpeedScale() const noexcept;
    //! 丸まっているか (当たりを球にしている間は true)
    [[nodiscard]] bool IsCurled() const noexcept;
    [[nodiscard]] GL::Player::PlayerMotion Motion() const noexcept;
    //! 世界の rootPosition にある当たりのカプセルの下の球を返す (立ち姿と玉で同じ球)
    [[nodiscard]] NS::Sphere SlamBallAt(const NS::Vector3& rootPosition) const noexcept;

    // =========================================================================
    // 物理挙動・速度制御（重力・加速・減速・ジャンプ）
    // =========================================================================
    //! 先行入力とコヨーテ猶予のタイマーを 1 フレーム進める
    void TickTimers(float dt) noexcept;
    //! 入力の向きへ加速する。加速の上限は MaxSpeed × 倒し具合で、歩き速度を下回らない
    void AccelerateToInputDirection(float dt) noexcept;
    //! ダッシュと反動の後の落下で、空中の操作が戻る経過を 1 フレーム進める
    void AdvanceAirControlReturn(float dt) noexcept;
    //! 次の落下の空中の操作を 0 から戻し直す
    void StartAirControlReturn() noexcept;
    void ApplyFriction(float dt) noexcept;
    void ApplyBrake(float dt) noexcept;
    //! 接地かコヨーテ猶予の内で押されていれば跳ぶ
    void Jump(float dt) noexcept;
    //! 上昇中にボタンを離したフレームだけ縦速度を縮める
    void CutJumpRelease() noexcept;
    //! 上昇と下降で非対称な重力を当てる (頂点の近くは弱める)
    void Gravity(float dt) noexcept;
    //! 通常突進の滞空秒が通常突進の距離を進む秒と揃う強さの重力を当てる
    void TapSlamGravity(float dt) noexcept;
    void ReboundGravity(float dt) noexcept;
    //! 反動の間、入力の向きへ反動中の空中の加速度で加速する (入力からずれた速度は減らさない)
    void AccelerateDuringRebound(float dt) noexcept;
    //! 今の水平の速度を着いた速度として控え、こすり止めを始める
    void BeginSkid() noexcept;
    //! 着地時の速度を減らし、止まりきったら true を返す
    [[nodiscard]] bool AdvanceSkid() noexcept;
    //! 突進の 1 フレームを進める。溜めた突進は水平を BodySlamVelocity で書き直し、重力を当てる
    void UpdateBodySlam(float dt) noexcept;

    // =========================================================================
    // 崖掴まり・よじ登り
    // =========================================================================
    //! 縁を掴めるか試す。掴んだら true (呼び出し側は即 return する)
    [[nodiscard]] bool LedgeGrab() noexcept;
    //! 掴んでいる縁を取り直して位置を合わせ直す。縁を失ったら手を放して false を返す (呼び出し側は即 return する)
    [[nodiscard]] bool HoldLedge() noexcept;
    //! 掴まりからジャンプの初速を与えて落下へ移る。ジャンプしたら true (呼び出し側は即 return する)
    [[nodiscard]] bool LedgeJump() noexcept;
    //! 左右入力で縁に沿って動く。続いていない方向へは動かない
    void Shimmy(float dt) noexcept;
    //! よじ登りを始め、登りの状態へ移る
    void ClimbLedge() noexcept;
    void UpdateLedgeClimb(float dt) noexcept;
    //! 手を放し、その場から落下させる
    void DropLedge() noexcept;

    // =========================================================================
    // 体当たり（突進）の要求・開始・方向解決・反動軌道
    // =========================================================================
    //! 体当たりを要求する。charge01・overcharge01 は 0〜1 に丸め、非有限は 0
    //! 出せない要求は先行入力時間まで残し、突進中・身体の停止中は捨てる。空中発動は接地まで一度だけ
    void RequestBodySlam(float charge01, float overcharge01 = 0.0f) noexcept;
    //! 向きと縦速度 (m/s、上が正、非有限は 0、通常突進には無効) を添えて体当たりを要求する
    //! aimDirection は世界の水平成分を正規化し、無効なら方向指定なしとして扱う
    void RequestBodySlam(float charge01,
                         const NS::Vector3& aimDirection,
                         float launchVerticalSpeed = 0.0f,
                         float overcharge01 = 0.0f) noexcept;
    //! 突進中は予定の水平速度と身体の現在の縦速度、それ以外は身体の速度を返す
    [[nodiscard]] NS::Vector3 BodySlamVelocity() const noexcept;
    [[nodiscard]] GL::Level::BodySlamMotion SlamMotion() const noexcept;
    [[nodiscard]] GL::Level::ImpactSelf SelfForImpact() const noexcept;
    //! 突進を打ち切って通常移動へ戻す。突進中でなければ何もしない
    void CancelBodySlam() noexcept;
    //! 向きを解決して突進を始める。向きが決まらないか距離が 0 以下なら false
    [[nodiscard]] bool BodySlam() noexcept;
    //! 向きを添えずに要求した体当たり (通常突進) を出す水平の向き (入力・カメラの前・速度の順、無ければゼロ)
    [[nodiscard]] NS::Vector3 AimDirection() const noexcept;
    //! このフレームの終わりに放したら次のフレームに出る突進の正規化済みの水平の向き (決まらなければゼロ)
    [[nodiscard]] NS::Vector3 ReleaseDirection() const noexcept;
    //! 反動の初速を与え、反動状態へ移る。無効な軌道なら false で何も変えない
    [[nodiscard]] bool BeginRebound(const GL::Player::ReboundArc& arc) noexcept;
    //! 反動軌道の初速 (m/s) を求める。曲線を組めなければゼロ
    [[nodiscard]] NS::Vector3 ReboundVelocityFor(const GL::Player::ReboundArc& arc) const noexcept;
    //! 丸まりを入れるか解く。丸まると当たりを球にして根を立ち姿の半長ぶん下げ、解くと立ち姿へ戻して上げる
    //! 縁に掴まっている間とよじ登っている間の true は受けない
    void SetCurled(bool curled) noexcept;

    // =========================================================================
    // カメラ追従・コース進行・体力
    // =========================================================================
    [[nodiscard]] GL::Player::CameraTargetState GetCameraTargetState() const;
    void WriteFollowState(NS::ThirdPersonFollow* vcam) const override;

    void InitAfterPlacement() override;

    //! MsgCourseStart の時の位置へ戻り、動きと命を最初の状態へ戻す (コース開始前は位置を動かさない)
    void Restart() noexcept;

    //! 命を amount 削る (下限 0)
    void ApplyDamage(int amount) noexcept;

    //! 命を 0 にして世界から消える
    void Die() noexcept;

    //! 調整値の「体力」を読み直して命を満タンへ戻す
    void ResetHealth() noexcept;

    [[nodiscard]] bool IsDead() const noexcept;
    [[nodiscard]] int Health() const noexcept;

protected:
    // =========================================================================
    // シーンのステップ・メッセージ受信
    // =========================================================================
    //! 即死・ゴール・コースのやり直し・操作の停止の知らせに応じる
    bool ReceiveMsg(const NS::Message& msg, NS::HitSensor* sender, NS::HitSensor* receiver) override;

    void OnInit() override;
    void OnAssetsLoaded() override;
    void OnAppear() override;
    void OnDisappear() noexcept override;
    void DecideStep() override;
    void AfterStateStep() override;
    void BodyStep() override;
    void VisualStep() override;

private:
    [[nodiscard]] NS::SensorVolume BodySensorVolume(const NS::Matrix& rootWorld) const noexcept;
    NS::HitSensor* m_bodyHitSensor = nullptr; // 体のセンサー (非所有)。持つのは HitSensorsObj

    struct BodySlamRequest
    {
        float bufferRemaining = 0.0f; // 先行入力の残り秒
        bool spent = false;           // 空中で発動してから接地していないか
        float charge01 = 0.0f;        // 0..1
        float overcharge01 = 0.0f;    // 0..1
        NS::Vector3 dir{};            // 正規化済み
        bool hasDir = false;
        float verticalSpeed = 0.0f; // 放つ瞬間の縦の速さ (m/s)。hasDir が偽の間は読まない
        NS::Vector3 aimDir{};       // 押したフレームに控えた狙いの向き (正規化済み)
        float aimAge = 0.0f;        // 狙いを控えてからの経過秒
    };

    struct ChargeObservation
    {
        bool held = false;
        GL::Level::AimLine aimLine{};
        bool hasAimLine = false;
        GL::Level::SlamLineTarget aimTarget{};
        bool hasAimTarget = false;
    };

    // =========================================================================
    // 内部ヘルパー：移動・エアダッシュ・地形反動
    // =========================================================================
    void MoveBody(float dt) noexcept;
    [[nodiscard]] bool CanStartAirDash() const noexcept;
    [[nodiscard]] bool BeginAirDash() noexcept;
    [[nodiscard]] float AirDashSpeed() const noexcept;
    void TryBeginTerrainRebound() noexcept;
    //! @brief 遮った面 normal で反動を始める。向きを表へ割り当て、当てた速さで返し、欄「反動の止めの最短のフレーム数」だけ止める
    //! @details 入力補正はここでは掛けない。止めが明けたフレームに ChooseReboundAtRelease が掛ける
    //! @param[in] baseSpeed 返る速さ (m/s)。エアダッシュならダッシュの速さに地形の反動の倍率を掛けた値、反動の続きなら今の速さ
    void BeginTerrainRebound(const NS::Vector3& incoming, const NS::Vector3& normal, float baseSpeed) noexcept;
    //! @brief DecideStep から毎フレーム呼ぶ。反動の止めの間に左スティックが動いていたら、欄「反動の止めの上限のフレーム数」まで止めを 1 フレーム延ばす
    void ExtendReboundStop() noexcept;
    //! @brief 止めが明けたフレームに 1 度だけ呼ぶ。今の入力で返る向きを選んで入力補正を掛け、合った時は弱い束を強い束に替える
    void ChooseReboundAtRelease() noexcept;
    //! 今の入力で、最後の反動の返る向きを表から選ぶ。入力が無ければ割り当てた向きのまま
    [[nodiscard]] GL::Level::ReboundChoice ChooseReboundInput() const noexcept;
    //! @brief 選んだ向きを掛ける。合った時は速さを欄「向きが合った時の返る速さの倍率」で強め、変えた時は飛んでいる
    //! 速さを保って向きを回し、走っている演出を弱めて振動を重ねる
    void ApplyReboundChoice(const GL::Level::ReboundChoice& choice) noexcept;
    //! 欄「向きの表の分割数」と「表に傾きの段を入れる」で作った反動の向きの表
    [[nodiscard]] std::vector<NS::Vector3> ReboundTable() const;
    //! 今のカメラから見た画面の右・上・見る向き。カメラが無ければ水平の前の向きから作る
    [[nodiscard]] GL::Level::ScreenAxes ReboundScreenAxes() const noexcept;
    //! 今の入力を、カメラから見た向きのまま、返る向き assigned に垂直な面の向きにする。入力が無ければゼロ
    [[nodiscard]] NS::Vector3 ReboundShiftInput(const NS::Vector3& assigned) const noexcept;
    //! @brief 地形の反動の束を始める。full なら強い束、偽なら弱い束。音は鳴らさない
    //! @details 地形には相手のセンサーが無いので、自分の体のセンサーを両方に渡す。向きを使う項目は向き無しになる
    void StartTerrainReboundReaction(const NS::Vector3& contact, bool full) noexcept;
    //! 入力補正で向きを変えた時の演出。走っている演出と残像を弱め、振動の束を重ねる
    void StartShiftedReboundReaction() noexcept;
    //! 欄「反動の線を描く」が真なら、最後の地形の反動の向きの線と、今の段の輪を描く
    void DrawTerrainRebound() const;
    void DropPresses() noexcept;
    // =========================================================================
    // 内部ヘルパー：突進・衝突裁定・命中反応・丸まり切り替え
    // =========================================================================
    void AdvanceBodySlamTravel(const NS::Vector3& delta) noexcept;
    void EndBodySlam() noexcept;
    [[nodiscard]] float BodySlamAimBlend01() const noexcept;
    [[nodiscard]] float BodySlamSpeed() const noexcept;
    [[nodiscard]] NS::Vector3 BlendedAimDirection() const noexcept;
    void ApplyImpactOrders(const GL::Level::ImpactAnswer& answer) noexcept;
    void ReleaseTackleHit() noexcept;
    void StopTackleHitEffects() noexcept;
    [[nodiscard]] bool IsHeldByTackleHit() const noexcept;
    [[nodiscard]] NS::HitSensor* LastTackleTargetSensor() const noexcept;
    void DecideBeforeContact(const GL::Level::ImpactAnswer& answer) noexcept;
    [[nodiscard]] bool ContinuesBeforeContact(const GL::Level::ImpactRecord& impact) const noexcept;
    [[nodiscard]] bool AcceptsBodySlamRequest() const noexcept;
    void ChangeCurled(bool curled) noexcept;
    void UncurlWhenSettled() noexcept;
    void SetMaxSpeedScale(float scale) noexcept;
    [[nodiscard]] bool TryMoveInput(NS::Vector3& outDirection, float& outTopSpeed) const noexcept;
    void MarkBodySlamAim() noexcept;
    // =========================================================================
    // 内部ヘルパー：チャージ観測・接地復帰・状態進行・アニメーション選択
    // =========================================================================
    void ObserveCharge(bool held);
    void AdvanceCharge(float dt);
    void ApplyChargeSway(float dt);
    void CancelCharge() noexcept;
#if !defined(NS_SHIPPING)
    void DrawChargeRing() const;
#endif
    void RestoreOnLanding() noexcept;
    void RestoreWhileGrounded() noexcept;
    void LaunchJump() noexcept;
    void PrepareStateStep();
    void FinishStateStep(float dt);
    [[nodiscard]] bool PrepareOrSkipStateStep();
    [[nodiscard]] bool FindLedgeTopAt(const NS::Vector3& hangPos,
                                      float& outTop,
                                      NS::Vector3& outFaceNormal,
                                      NS::Vector3& outHangPos) const noexcept;
    [[nodiscard]] const NS::AnimClip* ChooseClip(float lateralSpeed) const noexcept;
    [[nodiscard]] float ChoosePlaybackSpeed(const NS::Anim::AnimClip* clip, float lateralSpeed) const noexcept;
    // =========================================================================
    // メンバ変数：所有コンポーネント・サブシステム・体力
    // =========================================================================
    std::unique_ptr<GL::Player::PlayerParams> m_params;
    std::unique_ptr<GL::Player::PlayerClips> m_clips;
    std::unique_ptr<NS::PlayerInput> m_input;
    std::unique_ptr<NS::Body> m_body;
    std::unique_ptr<NS::Collider> m_collider;
    std::unique_ptr<GL::Player::PlayerAppearance> m_appearance;
    std::unique_ptr<GL::Level::ImpactResolver> m_resolver;
    std::unique_ptr<GL::Level::TargetMarker> m_targetMarker;
    std::unique_ptr<GL::Level::SlamArrow> m_slamArrow;
    std::unique_ptr<GL::Player::ImpactEffects> m_impactEffects;
    std::unique_ptr<GL::Player::PlayerSounds> m_playerSounds;
    bool m_slowedWorld = false;
    struct BeforeContactEntry
    {
        NS::SceneActor* target = nullptr;
        GL::Level::HitTier tier = GL::Level::HitTier::Center;
        int startFrame = 0;  // 状態の最初の歩に当てる動作のフレーム
        NS::Vector3 point{}; // 予測した玉が相手に触れる時の玉の中心 (世界座標)
    };
    BeforeContactEntry m_beforeContact;
    NS::StateMachineOf<PlayerActor>* m_states = nullptr; // 基底が所有し、OnInit の後から有効
    GL::Level::Health m_health;
    std::optional<NS::Vector3> m_courseStartPosition; // カプセル中心の世界座標

    // =========================================================================
    // メンバ変数：通常移動・ジャンプ・突進・反動・チャージ・崖掴まり状態
    // =========================================================================
    bool m_inputInjected = false;
    bool m_stateStepPrepared = false;
    bool m_prevJumpHeld = false;
    int m_jumpsRemaining = 1;
    float m_coyoteTimer = 0.0f; // コヨーテ猶予の残り秒
    float m_bufferTimer = 0.0f; // 先行ジャンプ入力の残り秒
    bool m_airControlReturning = false;
    float m_airControlReturnElapsed = 0.0f; // 秒

    float m_maxSpeedScale = 1.0f;

    GL::Player::BodySlamStatus m_slam;
    BodySlamRequest m_request;
    GL::Player::ReboundStatus m_rebound;
    GL::Player::SkidStatus m_skid;
    GL::Player::AirDashStatus m_airDash;
    GL::Player::ChargeStatus m_charge;
    ChargeObservation m_chargeObserved;
    int m_overchargeCount = 0; // 紫に入った回数。溜めを捨てても戻さない

    NS::Vector3 m_facingDir{0.0f, 0.0f, 0.0f}; // 掴む水平の向き
    float m_lastMoveDistance = 0.0f;           // 直前の Move で動いた距離 (m)
    float m_ledgeTopY = 0.0f;
    NS::Vector3 m_ledgeFaceNormal{0.0f, 0.0f, 0.0f}; // 掴んでいる面の外向き水平法線
    NS::Vector3 m_ledgeMantleStart{0.0f, 0.0f, 0.0f};
    NS::Vector3 m_ledgeMantleEnd{0.0f, 0.0f, 0.0f};
    float m_ledgeMantleTimer = 0.0f; // 秒

    GL::Player::PlayerEvents m_playerEvents;
};

[[nodiscard]] PlayerActor* FindPlayer(NS::ActorList& objects) noexcept;
