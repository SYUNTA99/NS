#pragma once

#include "Game/Level/Health.h"
#include "Game/Level/ImpactInputJudge.h"
#include "Game/Level/SlamAim.h"
#include "Game/Level/TackleGhost.h"
#include "Game/Player/CameraTargetState.h"
#include "Game/Player/PlayerEvents.h"
#include "Game/Player/PlayerStatus.h"
#include "Game/Player/ReboundArc.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/Sphere.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/StateMachineOf.h"

#include "NSlib/Core/Types.h"
#include <optional>
#include <string>
#include <string_view>

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
        class ChargeEffects;
        class ImpactEffects;
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
    } // namespace Level
} // namespace GL

//! @brief 固定の部品を OnInit で組むプレイヤー
//! @details 衝突の観測・裁定を身体の移動より先に行う。死とゴールは Scene の CourseDirector に通知する
class PlayerActor : public NS::SceneActor
{
public:
    PlayerActor() noexcept;
    ~PlayerActor() override;

    PlayerActor(const PlayerActor&) = delete;
    PlayerActor& operator=(const PlayerActor&) = delete;
    PlayerActor(PlayerActor&&) = delete;
    PlayerActor& operator=(PlayerActor&&) = delete;

    NS_REFLECT_DECLARE()
    //! 基底が所有する自機の状態機械。OnInit が組むので、Init の後から立ちの状態に居る
    [[nodiscard]] NS::StateMachineOf<PlayerActor>& States() noexcept { return *m_states; }
    [[nodiscard]] const NS::StateMachineOf<PlayerActor>& States() const noexcept { return *m_states; }
    [[nodiscard]] NS::PlayerInput& Input() noexcept { return *m_input; }
    [[nodiscard]] const NS::PlayerInput& Input() const noexcept { return *m_input; }
    //! 速度・接地を持つ身体の部品。部品名は保存の鍵なので "Movement" のまま
    [[nodiscard]] NS::Body& Body() noexcept { return *m_body; }
    [[nodiscard]] const NS::Body& Body() const noexcept { return *m_body; }
    [[nodiscard]] NS::Collider& Collider() noexcept { return *m_collider; }
    [[nodiscard]] const NS::Collider& Collider() const noexcept { return *m_collider; }
    [[nodiscard]] GL::Player::PlayerParams& Params() noexcept { return *m_params; }
    [[nodiscard]] const GL::Player::PlayerParams& Params() const noexcept { return *m_params; }
    [[nodiscard]] GL::Player::PlayerClips& Clips() noexcept { return *m_clips; }
    [[nodiscard]] const GL::Player::PlayerClips& Clips() const noexcept { return *m_clips; }
    [[nodiscard]] const GL::Level::ImpactInputJudge& ChargeJudge() const noexcept;
    //! 保持中の狙いの線を返す。入力方向ではなく、効果を掛ける前のカメラ方向を使う
    //! 押していない、カメラ管理なし、無効な方向では false で outLine を変更しない
    [[nodiscard]] bool TryGetAimLine(GL::Level::AimLine& outLine) const noexcept;
    //! 揺れを足す前の線で狙う相手を返す。相手が無い場合は false で outTarget を変更しない
    [[nodiscard]] bool TryGetAimTarget(GL::Level::SlamLineTarget& outTarget) const noexcept;
    //! 溜めすぎの揺れを足した線で、最初に触れる相手を返す
    //! 枠と縦速度は TryGetAimTarget の相手を使う。相手が無い場合は false で outTarget を変更しない
    [[nodiscard]] bool TryGetLineTarget(GL::Level::SlamLineTarget& outTarget) const noexcept;
    //! @brief 紫の揺れの位相 (ラジアン) を返す
    //! @details 溜めすぎのフレーム数から閉じた式で出す。紫になりきった瞬間に π/2 を通る。紫でない間は 0
    [[nodiscard]] float ChargeSwayPhase() const noexcept { return m_charge.SwayPhase(); }
    //! @brief 紫の揺れのずれを返す
    //! @details 狙いの線の右を正にした、相手の所 (相手がいなければ欄「相手がいない時に直す距離」の所) での横のずれ。
    //! 単位は m。紫でない間は 0
    [[nodiscard]] float ChargeSwayOffset() const noexcept { return m_charge.SwayOffset(); }
    //! @brief このフレームに紫の揺れが端を通った場合 true、それ以外の場合は false
    //! @details 位相が π/2 + nπ を通ったフレーム。紫になりきるフレームは必ず端を通る。紫に入ったフレームは数えない
    [[nodiscard]] bool ChargeSwayReachedEdge() const noexcept { return m_charge.SwayReachedEdge(); }
    //! 構えの縦倍率を返す。非保持時は 1。描画への反映は PlayerAppearance が行う
    [[nodiscard]] float StanceHeight() const noexcept;
    [[nodiscard]] GL::Level::ImpactResolver& Resolver() noexcept { return *m_resolver; }
    [[nodiscard]] GL::Player::PlayerAppearance& Appearance() noexcept { return *m_appearance; }
    [[nodiscard]] GL::Level::TargetMarker& TargetIndicator() noexcept { return *m_targetMarker; }
    [[nodiscard]] GL::Level::SlamArrow& SlamIndicator() noexcept { return *m_slamArrow; }
    [[nodiscard]] GL::Player::ChargeEffects& ChargeVisuals() noexcept { return *m_chargeEffects; }
    [[nodiscard]] GL::Player::ImpactEffects& ImpactVisuals() noexcept { return *m_impactEffects; }
    [[nodiscard]] GL::Level::TackleGhost& Ghost() noexcept { return m_tackleGhost; }
    [[nodiscard]] const GL::Level::TackleGhost& Ghost() const noexcept { return m_tackleGhost; }

    [[nodiscard]] NS::UpdatePhase Phase() const noexcept override { return NS::UpdatePhase::Player; }
    void ReadInput() override;
    using NS::SceneActor::Update;
    //! 入力の段を通さず chargeHeld を差し込み、1 固定ステップ進める
    void Update(bool chargeHeld);
    //! @brief 状態と水平の速さから再生するクリップと速度を選び、同居する Animation へ渡す
    //! @details Animation が無ければ何もしない。選んだクリップが無ければ立ちのクリップへ戻す
    void UpdateAnimation();
    void OnEndPlay() override;

    //! 世界座標系の目標移動方向と速度スケールを渡す。スケールは 0..1 に丸める
    void SetDesiredMove(const NS::Vector3& worldDir, float speedScale01) noexcept;
    //! 直近に渡された目標速度スケール 0..1
    [[nodiscard]] float DesiredSpeedScale() const noexcept;
    //! 直近に渡された世界座標系の目標移動方向。長さは入力の強さのままで正規化されていない
    [[nodiscard]] NS::Vector3 DesiredDirection() const noexcept;
    //! 掴まり中の生ローカル入力で各成分は -1..1。SetDesiredMove とは別に持つ
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
    //! @brief 走行速度に SetMaxSpeedScale の倍率を掛けた最高速度。負になる場合は 0
    //! @details 負のまま返すと EndBodySlam の頭打ちが cap / speed で負の倍率になり、突進明けに水平の向きが反転する
    [[nodiscard]] float MaxSpeed() const noexcept;
    [[nodiscard]] float RunSpeed() const noexcept;
    //! 先に根を出現位置へ置いて呼ぶ。移動・命中・丸まりを初期状態へ戻す
    //! 根は動かさず、描く形は前フレームの倍率も含めて 1 に戻す
    void ResetState() noexcept;
    //! 着地でジャンプ回数を戻し、接地中はコヨーテ猶予と突進の使用済みを戻す
    void SyncGroundState() noexcept;
    //! 自機だけの通知の受け口。身体の Events() は接地の 2 件を返すので名前を分ける
    [[nodiscard]] GL::Player::PlayerEvents& PlayerEventsRef() noexcept { return m_playerEvents; }

    [[nodiscard]] bool IsBodySlamming() const noexcept;
    //! 突進の進み具合 0..1。突進中でなければ 0
    [[nodiscard]] float BodySlamProgress01() const noexcept;
    [[nodiscard]] float BodySlamCharge01() const noexcept { return m_slam.Charge01(); } //!< 発動時の溜め量 0..1
    //! 発動時の溜めすぎの深さ 0..1。赤で放した突進と通常突進は 0
    [[nodiscard]] float BodySlamOvercharge01() const noexcept { return m_slam.Overcharge01(); }
    //! 溜めた突進を終える水平の距離。欄「突進距離」の値で、単位は m
    [[nodiscard]] float BodySlamDistance() const noexcept;
    //! @brief 最後に出した突進の水平の向きを返す
    //! @details 突進の間は向きが変わらない。突進が終わった後も、次の突進を出すまで残す
    //! @return 正規化済みの向き。突進を出す前と ResetState の後はゼロ
    [[nodiscard]] NS::Vector3 BodySlamDirection() const noexcept { return m_slam.Direction(); }
    [[nodiscard]] bool IsRebounding() const noexcept;
    [[nodiscard]] bool IsSkidding() const noexcept;
    //! 外れで着地後に滑って止まる場合 true、それ以外の場合は false。滑るフレーム数が 0 なら直後に立つ
    [[nodiscard]] bool SkidsOnLanding() const noexcept;
    //! 身体が有効で、命中の止め・反動待ちの外なら true、それ以外の場合は false
    [[nodiscard]] bool CanMoveBody() const noexcept;
    //! 最後に始めた反動の水平の向き。正規化済み。反動を始める前と ResetState の後はゼロ
    [[nodiscard]] NS::Vector3 ReboundDirection() const noexcept { return m_rebound.Direction(); }
    //! 最後に出した突進の角速度 (度/秒)。突進を出す前と ResetState 後は 0
    [[nodiscard]] float BodySlamSpinSpeed() const noexcept { return m_slam.SpinSpeed(); }
    //! 最後に始めた反動の角速度 (度/秒)。開始前と ResetState 後は 0
    [[nodiscard]] float ReboundSpinSpeed() const noexcept { return m_rebound.SpinSpeed(); }
    //! @brief 最後に始めた反動の外れの回り方を返す
    //! @details 外れの反動の時だけ値を持つ。書くのは BeginRebound
    [[nodiscard]] const std::optional<GL::Player::MissTumble>& ReboundMissTumble() const noexcept
    {
        return m_rebound.Tumble();
    }
    //! @brief 反動を始めた回数を返す
    //! @details 見た目が新しい反動の始まりを知るために読む。ResetState では戻さない
    [[nodiscard]] NS::uint32 ReboundCount() const noexcept { return m_rebound.Count(); }
    //! @brief こすって止まる間の、着いた水平の速さに掛けている今の倍率を返す
    //! @return 0〜1。こすって止まる状態でない間は 1
    [[nodiscard]] float SkidSpeedScale() const noexcept;
    //! 丸まっているか。正は当たりの部品の形で、当たりを球にしている間は真
    [[nodiscard]] bool IsCurled() const noexcept;
    //! 今の状態から、突進中・反動中・こすり中・丸まり・動けるかを組んで返す
    [[nodiscard]] GL::Player::PlayerMotion Motion() const noexcept;
    //! 世界の rootPosition にある当たりのカプセルの下の球を返す
    //! 丸まりで下端を揃えるため、立ち姿と玉で同じ球になる。ChangeCurled と判定の基準を揃える
    [[nodiscard]] NS::Sphere SlamBallAt(const NS::Vector3& rootPosition) const noexcept;

    //! 先行入力とコヨーテ猶予のタイマーを 1 フレーム進める
    void TickTimers(float dt) noexcept;
    //! @brief 入力の向きへ加速する。入力が無ければ何もしない
    //! @details 加速の上限は MaxSpeed × 倒し具合で、歩き速度を下回らない。空中は空中の加速度を使う
    void AccelerateToInputDirection(float dt) noexcept;
    //! 手を放した時の減速度で水平の速さを減らす
    void ApplyFriction(float dt) noexcept;
    //! ブレーキの減速度で水平の速さを減らす
    void ApplyBrake(float dt) noexcept;
    //! 接地かコヨーテ猶予の内で押されていれば跳ぶ
    void Jump(float dt) noexcept;
    //! 上昇中にボタンを離したフレームだけ縦速度を縮める
    void CutJumpRelease() noexcept;
    //! 上昇と下降で非対称な重力を当てる。頂点の近くは弱める。強さは ChooseGravity が PlayerParams::Gravity から選ぶ
    void Gravity(float dt) noexcept;
    //! 通常突進だけに当てる重力。滞空秒が欄「通常突進の距離」を進む秒と揃う強さにする
    void TapSlamGravity(float dt) noexcept;
    //! PlayerParams::ReboundGravity を使って重力を当てる。初速の計算と同じ値を使う
    void ReboundGravity(float dt) noexcept;
    //! @brief 反動の間、入力の向きへ反動中の空中の加速度で加速する。入力が無ければ何もしない
    //! @details 接地の印に依らずこの加速度を使い、入力の向きからずれた速度は減らさない
    void AccelerateDuringRebound(float dt) noexcept;
    //! 外れの着地からこすって止まり始める。今の水平の速度を着いた速度として控え、経過を 0 にする
    void BeginSkid() noexcept;
    //! 着地時の速度を MissSkidSpeedScale で減らす。止まりきった場合 true、それ以外の場合は false
    [[nodiscard]] bool AdvanceSkid() noexcept;
    //! @brief 突進の 1 フレームを進める。溜めた突進は水平を BodySlamVelocity で書き直し、重力を当てる
    //! @details 突進の間の水平の書き手はここだけ
    void UpdateBodySlam(float dt) noexcept;

    //! 縁を掴めるか試す。掴んだ場合 true、それ以外の場合は false。true なら呼び出し側は即 return する
    [[nodiscard]] bool LedgeGrab() noexcept;
    //! @brief 掴んでいる縁を取り直し、その高さへ位置を合わせ直す
    //! @details 重力は当てない。縁の高さが変われば追い、失ったら手を放す
    //! @return 縁が続いている場合 true、それ以外の場合は false。false なら呼び出し側は即 return する
    [[nodiscard]] bool HoldLedge() noexcept;
    //! @brief 掴まりからジャンプの縦の初速を与えて落下へ移る
    //! @return ジャンプが押された場合 true、それ以外の場合は false。true なら呼び出し側は即 return する
    [[nodiscard]] bool LedgeJump() noexcept;
    //! 左右入力で縁に沿って動く。続いていない方向へは動かない
    void Shimmy(float dt) noexcept;
    //! よじ登りを始める。2 段補間の始点と終点を決めて登りの状態へ移る
    void ClimbLedge() noexcept;
    void UpdateLedgeClimb(float dt) noexcept;
    //! 手を放し、その場から落下させる
    void DropLedge() noexcept;

    //! 体当たりを要求する。charge01・overcharge01 は 0〜1 に丸め、非有限は 0
    //! 出せない要求は先行入力時間まで残す。突進中・身体の停止中は捨てる
    //! 空中発動は接地まで一度だけ。地上発動は回数を消費しない。向きは BodySlam が決める
    void RequestBodySlam(float charge01, float overcharge01 = 0.0f) noexcept;
    //! 向きと縦速度を添えて要求する。受付条件は向きを添えない RequestBodySlam と同じ
    //! aimDirection は世界の水平成分を正規化する。無効なら方向指定なしとして扱う
    //! launchVerticalSpeed は m/s、上が正、非有限は 0。通常突進には効かない
    //! 溜め量と溜めすぎの深さは 0〜1 に丸め、非有限は 0
    void RequestBodySlam(float charge01,
                         const NS::Vector3& aimDirection,
                         float launchVerticalSpeed = 0.0f,
                         float overcharge01 = 0.0f) noexcept;
    //! 突進中は予定の水平速度と身体の現在の縦速度、それ以外は身体の速度を返す
    //! 壁で消える実速度を使うと衝突を先読みできないため、裁定にはこの速度を使う
    [[nodiscard]] NS::Vector3 BodySlamVelocity() const noexcept;
    //! 当たりの裁定へ渡す突進の今。突進中か・BodySlamVelocity・突進を出した時の溜めと溜めすぎ
    [[nodiscard]] GL::Level::BodySlamMotion SlamMotion() const noexcept;
    //! 突進を打ち切って通常移動へ戻す。突進中でなければ何もしない
    void CancelBodySlam() noexcept;
    //! @brief 向きを解決して突進を始める
    //! @details 向きは要求に添えた向き。添えていなければ AimDirection の向きに、押したフレームの控え
    //! (MarkBodySlamAim) を控えてからの秒に応じて混ぜる。
    //! 溜めた突進の縦の速さは要求に添えた値で、向きを添えていなければ 0。ジャンプの途中の縦の速さは持ち越さない。
    //! 通常突進は欄「通常突進の上向き初速」
    //! @return 向きが決まらないか距離が 0 以下の場合 false、それ以外の場合は true
    [[nodiscard]] bool BodySlam() noexcept;
    //! @brief 向きを添えずに要求した体当たり (通常突進) を出す水平の向きを返す
    //! @details 入力・カメラの前・速度の順に見て、どれも無ければゼロ。
    //! 溜めて放した突進は、狙いの線を控えていればその向きを添えるので、この向きへは出ない
    [[nodiscard]] NS::Vector3 AimDirection() const noexcept;
    //! 反動の初速を与え、反動状態へ移る。無効な軌道なら false で何も変えない
    //! 反動中はジャンプできず、専用の空中加速度を使う。着地後は真ん中なら立ち、外れなら滑りへ移る
    [[nodiscard]] bool BeginRebound(const GL::Player::ReboundArc& arc) noexcept;
    //! PlayerParams::ReboundGravity と LaunchArcInitialVelocity で初速 (m/s) を求める
    //! 高さ・距離・方向・重力から曲線を組めなければ 0
    [[nodiscard]] NS::Vector3 ReboundVelocityFor(const GL::Player::ReboundArc& arc) const noexcept;
    //! @brief 丸まりを入れるか解く
    //! @details 押している間は毎フレーム true が入る。自分で解くので、false はプレイを終える時だけ渡す。
    //! 丸まると当たりを球にして根を立ち姿の半長ぶん下げ、解くと立ち姿へ戻して上げる。
    //! 縁に掴まっている間とよじ登っている間の true は受けない
    void SetCurled(bool curled) noexcept;

    [[nodiscard]] GL::Player::CameraTargetState GetCameraTargetState() const;

    //! コースの進行役を用意する。全ての配置物が揃った後に呼ばれる
    void InitAfterPlacement() override;

    //! @brief MsgCourseStart の時の位置へ戻り、動きと命を最初の状態へ戻す
    //! @details コースが始まる前は、DefaultSpawnPosition の位置へ戻す
    void Restart() noexcept;
    [[nodiscard]] NS::Vector3 DefaultSpawnPosition() const noexcept;

    //! 命を amount 削る。下限 0
    void ApplyDamage(int amount) noexcept;

    //! @brief 即死する。命を 0 にして世界から消える
    //! @details 世界から外すだけの Disappear は命に触らない。死は落下死などの知らせだけが起こす
    void Die() noexcept;

    //! @brief 命を満タンへ戻す。プレイ突入とリスタートで呼ばれる
    //! @details 満タンは呼ぶたびに調整値の「体力」から読む。エディタで書き換えた値が次の再生から効く
    void ResetHealth() noexcept;

    [[nodiscard]] bool IsDead() const noexcept;
    [[nodiscard]] int Health() const noexcept;

protected:
    //! @brief 即死・ゴール・コースのやり直し・操作の停止の知らせに応じる
    //! @details 操作の停止は PlayerInput の止め (SetLocked) へ写し、止める時は CancelCharge で溜めを捨てる
    bool ReceiveMsg(const NS::Message& msg, NS::HitSensor* sender, NS::HitSensor* receiver) override;

    void OnInit() override;
    //! 基底の作る物に続けて、読んだ姿のメッシュと倍率を Model へ差す
    void OnAssetsLoaded() override;
    //! ロックオンの枠と突進の矢印を重ね描きへ入れる。消えている間は描く側が IsOverlayVisible で飛ばす
    //! 残像は Model と同じく、出ている間だけ描く物へ入れる
    void OnStart() override;
    void OnAppear() override;
    void OnDisappear() noexcept override;
    //! PlayerInput の体当たりの押しと狙いの観測 (ObserveCharge)、
    //! 体当たりの衝突の観測 (ImpactResolver::ObserveImpact)。副作用は無い
    void ObserveStep() override;
    //! @brief 溜めを進め (AdvanceCharge)、衝突の裁定を出して知らせを送る (ImpactResolver::StepState)
    //! @details 裁定の答えにある自機を動かす事 (突進の打ち切り・反動・貫通の速度) は、ここで自機がする。
    //! 裁定役が外されていれば、走っている当たりのタイムラインを捨てる (ImpactResolver::CancelImpact)
    void DecideStep() override;
    //! 突進の発動、状態機械の 1 歩、丸まりの解除と押下の消費。CanMoveBody が偽の間は押下の消費だけ
    void StateStep() override;
    void BodyStep() override;
    //! 狙いの表示・命中反応・外見・溜め・命中エフェクトの順で更新する
    void VisualStep() override;

private:
    //! 体当たりの要求と、押したフレームの狙いの控え
    struct BodySlamRequest
    {
        float bufferRemaining = 0.0f; // 出せないフレームの押しを覚える残り秒
        bool spent = false;           // 空中で発動してから接地していないか
        float charge01 = 0.0f;        // 要求された溜め量 0..1
        float overcharge01 = 0.0f;    // 要求された溜めすぎの深さ 0..1
        NS::Vector3 dir{};            // 要求に添えた出す向き。正規化済み
        bool hasDir = false;
        float verticalSpeed = 0.0f; // 溜めた突進を放つ瞬間の縦の速さ。hasDir が偽の間は読まない
        NS::Vector3 aimDir{};       // 押したフレームに控えた狙いの向き。正規化済み
        float aimAge = 0.0f;        // 狙いを控えてからの経過秒
    };

    //! 観測の段に控えた押しと狙い。確定するのは AdvanceCharge で、確定した値は m_charge に写す
    struct ChargeObservation
    {
        bool held = false;
        GL::Level::AimLine aimLine{};
        bool hasAimLine = false;
        GL::Level::SlamLineTarget aimTarget{};
        bool hasAimTarget = false;
    };

    //! 固定ステップごとに一度呼ぶ。移動後に接地と突進距離を更新する
    void MoveBody(float dt) noexcept;
    //! 稼働していないフレームでも、そのフレーム限りの入力は落とす。残すと再開した時に古い押下が効く
    void SkipBodyStep() noexcept;
    //! 突進の進んだ距離を足し、距離を使い切るか進めなくなったら突進を終える
    //! @details 進めた距離は動かした後にしか出ないので、打ち切りの判定は状態でなくここに置く
    //! 受け取るのは直前の Move で実際に動いた量
    void AdvanceBodySlamTravel(const NS::Vector3& delta) noexcept;
    //! 突進を終える。水平の速さを MaxSpeed で切り、接地していれば走りへ、空中なら落下へ移す
    void EndBodySlam() noexcept;
    //! 控えた狙いを今の向きにどれだけ混ぜるか 0..1。巻き戻し秒までは 1、消える秒で 0
    [[nodiscard]] float BodySlamAimBlend01() const noexcept;
    //! 通常・溜めた突進の水平速度 (m/s) を返す。初速と更新に同じ値を使う
    [[nodiscard]] float BodySlamSpeed() const noexcept;
    //! 押している間に狙っている水平の向き。狙いの線があればその向き、無ければ AimDirection
    [[nodiscard]] NS::Vector3 ChargeAimDirection() const noexcept;
    //! @brief 裁定の答えにある、このフレームに自機がする事をする
    //! @details 突進を打ち切ってから、止めた自機を動かし直す。反動の曲線があれば反動を始め、
    //! 曲線にならない時と貫通は答えの速度を身体へ書く
    void ApplyImpactOrders(const GL::Level::ImpactAnswer& answer) noexcept;
    //! @brief 体当たりの要求を覚えてよい場合 true、それ以外の場合は false
    //! @details 突進中と CanMoveBody が偽の間は偽。ここで覚えた押しは止めの間に減らず、明けや空振りの後に 2 本目になる
    [[nodiscard]] bool AcceptsBodySlamRequest() const noexcept;
    //! 当たりの下端を保ち、立ち姿と球を切り替え、見た目の姿も同じフレームに持ち替えさせる
    //! 描画で動きに見えないよう、根は前フレームの位置も一緒にずらす
    void ChangeCurled(bool curled) noexcept;
    //! @brief 丸まりを解く
    //! @details 押されていない・突進中でない・直前のフレームを突進中で終えていない・突進の予約が無い・
    //! 接地している・上向きの速度が無い、が揃ったフレームに解く。縁を掴んだ時に解くのは PlayerActor::LedgeGrab
    void UncurlWhenSettled() noexcept;
    //! 走行速度に掛ける倍率を渡す。非有限値は無視して直前の値を残す
    void SetMaxSpeedScale(float scale) noexcept;
    //! @brief 動かす入力があれば、その水平の向きと加速の上限を返す
    //! @return 入力があり向きが決まった場合 true、それ以外の場合は false
    [[nodiscard]] bool TryMoveInput(NS::Vector3& outDirection, float& outTopSpeed) const noexcept;
    //! 押したフレームの狙いを控える。離すまでの遅れのぶん、向きを添えない発動はこの向きから始める
    void MarkBodySlamAim() noexcept;
    //! @brief このフレームの押しを控え、押していれば狙いの線と狙う相手を探して控える
    //! @details 控えるのは m_chargeObserved だけで、判定・速度・丸まりには触らない
    //! @param[in] held 体当たりのボタンを押しているか
    void ObserveCharge(bool held);
    //! 観測済みの保持・狙いを確定し、溜めと発動要求を進める。dt は秒
    void AdvanceCharge(float dt);
    //! 溜めすぎの横揺れを狙いに足し、線上の相手を再検索する。dt は秒
    void ApplyChargeSway(float dt);
    //! @brief 溜めを捨てる。放した扱いにはしないので、通常突進も溜めた突進も出ない
    //! @details 判定と狙いの控えを初めの値へ戻し、押しの印を偽、最高速度の倍率を 1 へ戻す。
    //! 構えは判定から答えるので 1 に戻る。丸まりは解かず、着地で解ける
    void CancelCharge() noexcept;
    //! プレイを終える時に溜めを捨て (CancelCharge)、丸まりを解く
    void EndCharge() noexcept;
#if !defined(NS_SHIPPING)
    //! 溜めている間、当たりの足元に溜め量で広がる輪を描く
    void DrawChargeRing() const;
#endif
    //! 突進の発動の判定を通れば突進を出す。状態機械を進める前に呼ぶ
    void PrepareStateStep();
    //! 状態機械を進めた後の控えの更新。丸まりを解く判定・長押しの控え・押下の消費・要求と狙いの経過を進める
    void FinishStateStep(float dt);
    //! @brief 掴まり位置から掴める縁を探す
    //! @param[in] hangPos 手を伸ばす元になるカプセル中心の位置
    //! @param[out] outTop 見つけた縁の上端の y。見つからない場合は書き換えない
    //! @return 手の高さ以下の帯に縁があり、登り先も塞がっていない場合 true、それ以外の場合は false
    [[nodiscard]] bool FindLedgeTopAt(const NS::Vector3& hangPos, float& outTop) const noexcept;
    // 状態と水平の速さから役の欄を選ぶ。字面が空の役は立ちの欄に代える
    [[nodiscard]] const NS::AnimClip* ChooseClip(float lateralSpeed) const noexcept;
    [[nodiscard]] float ChoosePlaybackSpeed(const NS::Anim::AnimClip* clip, float lateralSpeed) const noexcept;
    std::unique_ptr<GL::Player::PlayerParams> m_params;
    std::unique_ptr<GL::Player::PlayerClips> m_clips;
    std::unique_ptr<NS::PlayerInput> m_input;
    std::unique_ptr<NS::Body> m_body;
    std::unique_ptr<NS::Collider> m_collider;
    std::unique_ptr<GL::Player::PlayerAppearance> m_appearance;
    std::unique_ptr<GL::Level::ImpactResolver> m_resolver;
    std::unique_ptr<GL::Level::TargetMarker> m_targetMarker;
    std::unique_ptr<GL::Level::SlamArrow> m_slamArrow;
    std::unique_ptr<GL::Player::ChargeEffects> m_chargeEffects;
    std::unique_ptr<GL::Player::ImpactEffects> m_impactEffects;
    GL::Level::TackleGhost m_tackleGhost{this}; // 体当たりの残像。Model を借りて描く
    NS::StateMachineOf<PlayerActor>* m_states = nullptr; // 基底が所有する。OnInit が預けた直後から有効
    GL::Level::Health m_health;
    std::optional<NS::Vector3> m_courseStartPosition; // MsgCourseStart の時の位置。やり直しで戻る先

    bool m_prevJumpHeld = false;
    int m_jumpsRemaining = 1;
    float m_coyoteTimer = 0.0f; // コヨーテ猶予の残り秒
    float m_bufferTimer = 0.0f; // 先行ジャンプ入力の残り秒

    float m_maxSpeedScale = 1.0f; // 走行速度に掛ける倍率。書くのは AdvanceCharge と CancelCharge

    GL::Player::BodySlamStatus m_slam;
    BodySlamRequest m_request;
    GL::Player::ReboundStatus m_rebound;
    GL::Player::SkidStatus m_skid;
    GL::Player::ChargeStatus m_charge;
    ChargeObservation m_chargeObserved;
    // 紫に入った回数。紫になりきった時に揺れが来る端を毎回入れ替える。溜めを捨てても戻さない
    int m_overchargeCount = 0;

    NS::Vector3 m_facingDir{0.0f, 0.0f, 0.0f}; // 掴む向き。動こうとした水平の向きへ振り向きの速さで回る
    float m_lastMoveDistance = 0.0f;           // 直前の Move で動いた距離。縁を探す帯の上の余白
    float m_ledgeTopY = 0.0f;
    NS::Vector3 m_ledgeFaceNormal{0.0f, 0.0f, 0.0f}; // 掴んでいる面の外向き法線
    NS::Vector3 m_ledgeMantleStart{0.0f, 0.0f, 0.0f};
    NS::Vector3 m_ledgeMantleEnd{0.0f, 0.0f, 0.0f};
    float m_ledgeMantleTimer = 0.0f;

    GL::Player::PlayerEvents m_playerEvents;
};

//! 実体の配置物からプレイヤーを引く。無ければ nullptr
//! @param[in,out] objects 探す先の配置物。返した PlayerActor* から中身が書き換わる
[[nodiscard]] PlayerActor* FindPlayer(NS::ActorList& objects) noexcept;
