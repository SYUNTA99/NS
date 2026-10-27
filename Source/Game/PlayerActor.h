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

namespace GL::Player
{
        class PlayerParams;
        class PlayerClips;
        class PlayerAppearance;
        class ChargeEffects;
        class ImpactEffects;
} // namespace GL::Player

namespace GL::Level
{
        class ImpactResolver;
        class TargetMarker;
        class SlamArrow;
        struct BodySlamMotion;
        struct ImpactAnswer;
        struct ImpactRecord;
        struct ImpactSelf;
} // namespace GL::Level

//! @brief 固定の部品を OnInit で組むプレイヤー
//! @details 衝突の観測・裁定を身体の移動より先に行う。死とゴールは Scene の CourseDirector に通知する
class PlayerActor : public NS::SceneActor, public NS::IGetCameraTarget
{
public:
    PlayerActor() noexcept;
    ~PlayerActor() override;

    NS_REFLECT_DECLARE()
    // =========================================================================
    // コンポーネント・基本情報の参照
    // =========================================================================
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

    // =========================================================================
    // 更新・アニメーション・移動入力・ジャンプ状態
    // =========================================================================
    using NS::SceneActor::Update;
    //! 機器を読まずに chargeHeld を差し込み、1 固定ステップ進める。ほかの入力は部品の Set〜 で書いた値のまま
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
    //! コヨーテ猶予の残り秒。0 以下なら宙からは跳べない
    [[nodiscard]] float CoyoteRemaining() const noexcept { return m_coyoteTimer; }
    //! @brief 走行速度に SetMaxSpeedScale の倍率を掛けた最高速度。負になる場合は 0
    //! @details 負のまま返すと EndBodySlam の頭打ちが cap / speed で負の倍率になり、突進明けに水平の向きが反転する
    [[nodiscard]] float MaxSpeed() const noexcept;
    [[nodiscard]] float RunSpeed() const noexcept;
    //! 先に根を出現位置へ置いて呼ぶ。移動・溜め・命中・丸まりを初期状態へ戻す。プレイの終わりも同じ組を戻す
    //! 根は動かさず、描く形は前フレームの倍率も含めて 1 に戻す
    void ResetState() noexcept;
    //! 着地でジャンプ回数を戻し、接地中はコヨーテ猶予と突進の使用済みを戻す
    void SyncGroundState() noexcept;
    //! 自機だけの通知の受け口。身体の Events() は接地の 2 件を返すので名前を分ける
    [[nodiscard]] GL::Player::PlayerEvents& PlayerEventsRef() noexcept { return m_playerEvents; }

    // =========================================================================
    // 体当たり（突進）・反動・エアダッシュ・丸まりの状態と操作
    // =========================================================================
    //! 突進の状態か、突進を続ける当たる直前の状態に居る場合 true、それ以外の場合は false
    [[nodiscard]] bool IsBodySlamming() const noexcept;
    //! @brief 当たる直前の状態に居る場合 true、それ以外の場合は false
    //! @details 突進の線の先に予測した相手に触れる前で、その段の当たる直前の動作を始めてから、検知か予測の外れで
    //! 出るまで。移る予約の間も数える
    [[nodiscard]] bool IsInBeforeContact() const noexcept;
    //! @brief 当たる直前の状態に入った時に呼ぶ。予測した相手の段の当たる直前の動作を、残りの歩に合うフレームから始める
    //! @details 相手が居ない時は WARN を出して動作を始めない
    void BeginBeforeContact() noexcept;
    //! @brief 当たる直前の状態を出る時に呼ぶ。検知で出る時は何もせず、予測が外れて出る時は形と自機以外の止めと
    //! 返りを戻す
    void EndBeforeContact() noexcept;
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
    //! エアダッシュの状態に居る場合 true、それ以外の場合は false
    [[nodiscard]] bool IsAirDashing() const noexcept;
    //! 地形の面で返った反動の状態に居る場合 true、それ以外の場合は false
    [[nodiscard]] bool IsDashRebounding() const noexcept;
    //! 今出せるエアダッシュの回数。接地の間と、反動の補正の段に入った時に 1 に戻る
    [[nodiscard]] int AirDashesRemaining() const noexcept { return m_airDash.Remaining(); }
    //! エアダッシュと地形の反動の値。見た目・カメラ・開発用の線・Replay が読む
    [[nodiscard]] const GL::Player::AirDashStatus& AirDash() const noexcept { return m_airDash; }
    //! @brief エアダッシュの動作 (.actor の動作 "AirDash") を、今の状態に結んで始める。エアダッシュの状態の OnEnter が呼ぶ
    //! @details 動作の length がダッシュの長さで、状態は IsStateEnd で終わりを問う。ダッシュの速さもここで控え、
    //! このダッシュの間は途中で動作を読み直しても変えない
    //! @return 始めた場合 true。動作のタイムラインが無いか、Start が断った場合は false
    [[nodiscard]] bool StartAirDashAction() noexcept;
    //! @brief エアダッシュの 1 フレームの速度を書く。重力を掛けずに、向き × 控えたダッシュの速さにする
    //! @details エアダッシュの間の速度の書き手は、押したフレームの BeginAirDash と、ここと KeepAirDashExitSpeed と、
    //! 面に当たった時の BeginTerrainRebound だけ
    void KeepAirDashSpeed() noexcept;
    //! 当たらずに終わったエアダッシュの勢いを、向き × 欄「エアダッシュ後に残る速さ」で残す
    void KeepAirDashExitSpeed() noexcept;
    //! 接地していれば歩きへ、空中なら落下へ移す
    void ChangeToWalkOrFall() noexcept;
    //! @brief 地形の反動の 1 フレームを進める
    //! @details 操作なしの段は速度を保つ。補正の段はダッシュを 1 回戻し、重力・減速・曲げを掛ける。
    //! 補正の段を過ぎたら落下へ、登れる面に着いたら地上の状態へ移る
    void UpdateDashRebound(float dt) noexcept;
    //! @brief 反動のまま着いた場合 true、それ以外の場合は false
    //! @details 反動の着地を判定するのはここだけ。見た目と演出は移動の後に、反動の状態は次のフレームの頭に読む
    [[nodiscard]] bool IsReboundLanded() const noexcept;
    [[nodiscard]] bool IsSkidding() const noexcept;
    //! 外れで着地後に滑って止まる場合 true、それ以外の場合は false。滑るフレーム数が 0 なら直後に立つ
    [[nodiscard]] bool SkidsOnLanding() const noexcept;
    //! 出ていて、当たりの止めの外で、当たりの状態に居ない場合 true、それ以外の場合は false
    [[nodiscard]] bool CanMoveBody() const noexcept;
    //! @brief 当たりの状態に居る場合 true、それ以外の場合は false
    //! @details 検知の次のフレームに入り、止めが明けたフレームの決定の段で反動へ移るまで。移る予約の間も数える
    [[nodiscard]] bool IsInTackleHit() const noexcept;
    //! @brief 当たりの状態に入った時に呼ぶ。段の束を触れた点と 2 つのセンサーで始め、相手へ当たったを知らせる
    //! @details 貫通は束を始めた後に、止めの残りを欄「貫通の止め秒」の長さで上書きし、形を変えない。
    //! 相手が居ない時は WARN を出して束を始めず、止めないので次のフレームに明ける
    void BeginTackleHit() noexcept;
    //! @brief 反動の状態に入った時に呼ぶ。束「反動」を最後の当たりの触れた点と 2 つのセンサーで始める
    //! @details 束は同じ持ち主の沈みを跳ね返す。最後の当たりの相手が居なければ始めない
    void StartReboundReaction() noexcept;
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

    // =========================================================================
    // 物理挙動・速度制御（重力・加速・減速・ジャンプ）
    // =========================================================================
    //! 先行入力とコヨーテ猶予のタイマーを 1 フレーム進める
    void TickTimers(float dt) noexcept;
    //! @brief 入力の向きへ加速する。入力が無ければ何もしない
    //! @details 加速の上限は MaxSpeed × 倒し具合で、歩き速度を下回らない。空中は空中の加速度を使う
    void AccelerateToInputDirection(float dt) noexcept;
    //! ダッシュと反動の後の落下で、空中の操作が戻る経過を 1 フレーム進める。落下の状態だけが呼ぶ
    void AdvanceAirControlReturn(float dt) noexcept;
    //! 次の落下の空中の操作を 0 から戻し直す。エアダッシュと反動を始める所が呼ぶ
    void StartAirControlReturn() noexcept;
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
    //! @details 接地のフラグに依らずこの加速度を使い、入力の向きからずれた速度は減らさない
    void AccelerateDuringRebound(float dt) noexcept;
    //! 外れの着地からこすって止まり始める。今の水平の速度を着いた速度として控え、経過を 0 にする
    void BeginSkid() noexcept;
    //! 着地時の速度を MissSkidSpeedScale で減らす。止まりきった場合 true、それ以外の場合は false
    [[nodiscard]] bool AdvanceSkid() noexcept;
    //! @brief 突進の 1 フレームを進める。溜めた突進は水平を BodySlamVelocity で書き直し、重力を当てる
    //! @details 突進の間の水平の書き手はここだけ
    void UpdateBodySlam(float dt) noexcept;

    // =========================================================================
    // 崖掴まり・よじ登り
    // =========================================================================
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

    // =========================================================================
    // 体当たり（突進）の要求・開始・方向解決・反動軌道
    // =========================================================================
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
    //! 当たりの裁定へ渡す自機のシーンのセンサー・ルートの位置・除く相手 (自分)
    [[nodiscard]] GL::Level::ImpactSelf SelfForImpact() const noexcept;
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
    //! @brief このフレームの終わりに放したら、次のフレームに出る突進の水平の向きを返す
    //! @details 溜めの間は狙いの線の向き、しきい値の前と線の無い時は BlendedAimDirection。
    //! 玉の回る軸と溜めの演出はこの向きを読む
    //! @return 正規化済みの向き。決まらない場合はゼロ
    [[nodiscard]] NS::Vector3 ReleaseDirection() const noexcept;
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

    // =========================================================================
    // カメラ追従・コース進行・体力
    // =========================================================================
    [[nodiscard]] GL::Player::CameraTargetState GetCameraTargetState() const;
    //! GetCameraTargetState を追従カメラの欄へ写す。壊れた溜めと反動の値は WARN を出して渡さない
    void WriteFollowState(NS::ThirdPersonFollow* vcam) const override;

    //! コースの進行役を用意する。全てのアクターが揃った後に呼ばれる
    void InitAfterPlacement() override;

    //! @brief MsgCourseStart の時の位置へ戻り、動きと命を最初の状態へ戻す
    //! @details コースが始まる前は、位置を動かさない
    void Restart() noexcept;

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
    // =========================================================================
    // シーンのステップ・メッセージ受信
    // =========================================================================
    //! @brief 即死・ゴール・コースのやり直し・操作の停止の知らせに応じる
    //! @details 操作の停止は PlayerInput の止め (SetLocked) へ写し、止める時は CancelCharge で溜めを捨てる
    bool ReceiveMsg(const NS::Message& msg, NS::HitSensor* sender, NS::HitSensor* receiver) override;

    void OnInit() override;
    //! 基底の作る物に続けて、読んだ姿のメッシュと倍率を Model へ差し、溜めと当たりの絵を先に読む
    void OnAssetsLoaded() override;
    //! 残像・ロックオンの枠・突進の矢印は、入れたまま出ている間だけ描く
    void OnAppear() override;
    void OnDisappear() noexcept override;
    //! @brief 入力を読み、押しと狙い (ObserveCharge) と衝突 (ImpactResolver::ObserveImpact) を見てから、溜めを進め
    //! (AdvanceCharge)、衝突の裁定を出して知らせを送る (ImpactResolver::StepState)。終わりに状態機械の 1 歩を支度する
    //! @details 裁定の答えにある自機を動かす事 (突進の打ち切り・反動・貫通の速度) は、ここで自機がする。
    //! 止めの間は何もせず押しを明けへ持ち越す。CanMoveBody が真なら突進の発動を判定し、偽なら押しを下ろす。
    //! 偽の間は、当たりの状態のフレームだけ状態機械を進め、ほかは SkipStateStep で飛ばす
    void DecideStep() override;
    //! 突進の発動を判定したフレームにだけ、丸まりの解除・長押しの控え・押しを下ろす・要求と狙いの経過を進める
    void AfterStateStep() override;
    void BodyStep() override;
    //! 狙いの表示・命中反応・外見・溜め・命中エフェクトの順で更新する
    void VisualStep() override;

private:
    // 範囲が照合する体は移動の当たりと同じカプセル。寸法の正は Collider の欄で、センサーは毎回それを読む
    NS_SENSORS_BEGIN()
    NS_FOLLOW_SENSOR(m_bodySensor, GL::Level::SensorKind::PlayerBody, BodySensorVolume)
    NS_SENSORS_END()
    // 体のセンサーの世界座標の形。OnInit が m_collider を作った後にしか問われない
    [[nodiscard]] NS::SensorVolume BodySensorVolume(const NS::Matrix& rootWorld) const noexcept;

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

    //! DecideStep の頭で控えた押しと狙い。確定するのは AdvanceCharge で、確定した値は m_charge に写す
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
    //! 固定ステップごとに一度呼ぶ。移動後に接地と突進距離を更新し、面に当たったエアダッシュと反動を返す
    void MoveBody(float dt) noexcept;
    //! この歩にエアダッシュを出せる場合 true、それ以外の場合は false。押した瞬間で、回数が残り、出せる状態に居る
    [[nodiscard]] bool CanStartAirDash() const noexcept;
    //! @brief エアダッシュを始める。向きはスティック、倒していなければ自機の向き、無ければカメラの前
    //! @details 回数を 1 減らし、押したフレームの体の更新から速さが出るよう速度もここで書く。
    //! ダッシュの速さが 0 以下なら WARN を出して始めず、回数も減らさない
    //! @return 始めた場合 true
    [[nodiscard]] bool BeginAirDash() noexcept;
    //! @brief エアダッシュの速さ (m/s)。欄「エアダッシュの距離」を、動作 "AirDash" の length の固定フレームで進む速さ
    //! @details 動作から毎回引き直す。ダッシュの最中に読むのは、始めた時に控えた速さの方。
    //! 動作が無いか length が 0 か距離が非数なら 0
    [[nodiscard]] float AirDashSpeed() const noexcept;
    //! 今のスティックの向きを倒し続けた秒を数え、欄「行きたい向きとみなす秒」以上ならダッシュの向きにする。毎フレーム観測の段で呼ぶ
    void UpdateIntendedDirection(float dt) noexcept;
    //! @brief 体を動かした直後に呼ぶ。エアダッシュか地形の反動の間に面が遮ったら、その面で反動を始める
    //! @details 当て方が欄「反動に数える当て方の下限」より浅い面と、反動の間の登れる面は数えない
    void TryBeginTerrainRebound() noexcept;
    //! @brief 遮った面 normal で反動を始める。向きを表へ割り当て、返る速さを書く。ずらしは ApplyReboundShift が後で掛ける
    //! @param[in] baseSpeed 返る力の上限 (m/s)。エアダッシュならダッシュの速さに地形の反動の倍率を掛けた値、反動の続きなら今の速さ
    void BeginTerrainRebound(const NS::Vector3& incoming, const NS::Vector3& normal, float baseSpeed) noexcept;
    //! @brief 反動の操作なしの段の間に呼ぶ。スティックで隣の向きが選べたら、飛んでいる速さを保って向きを 1 段回す
    //! @details 掛けるのは反動 1 回に 1 度。当たる前から倒したままのスティックも数える
    void ApplyReboundShift() noexcept;
    //! 欄「向きの表の分割数」と「表に傾きの段を入れる」で作った反動の向きの表
    [[nodiscard]] std::vector<NS::Vector3> ReboundTable() const;
    //! 今のスティックを、カメラから見た向きのまま、返る向き assigned に垂直な面の向きにする。倒していなければゼロ
    [[nodiscard]] NS::Vector3 ReboundShiftInput(const NS::Vector3& assigned) const noexcept;
    //! @brief 地形の反動の束を、当て方の強さで選んで始める
    //! @details 地形には相手のセンサーが無いので、自分の体のセンサーを両方に渡す。向きを使う項目は向き無しになる
    void StartTerrainReboundReaction(const NS::Vector3& contact, float facing) noexcept;
    //! 欄「反動の線を描く」が真なら、最後の地形の反動の向きの線と、今の段の輪を描く
    void DrawTerrainRebound() const;
    //! 体を動かさないフレームでも、そのフレーム限りの押しは下ろす。残すと再開した時に古い押下が効く
    void DropPresses() noexcept;
    // =========================================================================
    // 内部ヘルパー：突進・衝突裁定・命中反応・丸まり切り替え
    // =========================================================================
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
    //! @brief 向きを添えない突進が出る水平の向き。AimDirection に押したフレームの控えを BodySlamAimBlend01 で混ぜる
    //! @return 正規化済みの向き。決まらない場合はゼロ
    [[nodiscard]] NS::Vector3 BlendedAimDirection() const noexcept;
    //! @brief 裁定の答えにある、このフレームに自機がする事をする
    //! @details 検知したら前の当たりの演出を止めて突進を打ち切り、検知の次のフレームに当たりの状態へ移る。
    //! 明けでは反動の曲線があれば反動を始め、曲線にならない時と貫通は答えの速度を身体へ書いて当たりの状態を出る
    void ApplyImpactOrders(const GL::Level::ImpactAnswer& answer) noexcept;
    //! @brief 止めが明けたフレームの決定の段に呼ぶ。明けの束 (紫で放した時は紫の明けの束も) を始め、
    //! 相手へ明けを知らせて、反動の曲線を裁定の答えに書かせる
    void ReleaseTackleHit() noexcept;
    //! @brief 当たりの束で始めた演出のうち、白・振動・カメラの返り以外を止める
    //! @details 描く揺れ・震え・形・残像・床の波・歪みの輪を止め、紫の明けで遅くした世界を普段の速さへ戻す
    void StopTackleHitEffects() noexcept;
    //! 当たりを検知してから、止めが明けて反動へ移るまで true
    [[nodiscard]] bool IsHeldByTackleHit() const noexcept;
    //! 最後の当たりの相手の体のセンサー。相手が居なければ nullptr
    [[nodiscard]] NS::HitSensor* LastTackleTargetSensor() const noexcept;
    //! @brief 裁定の予測から、当たる直前の状態へ入るか、外れた予測で突進へ戻るかを決める。ApplyImpactOrders の後に呼ぶ
    //! @details 入るのは突進の線の先に予測した相手の段に当たる直前の動作があり、検知まで動作の長さ以内の時。
    //! 入る時は前の当たりの演出を打ち切る。検知したフレームは何もしない
    void DecideBeforeContact(const GL::Level::ImpactAnswer& answer) noexcept;
    //! 当たる直前の状態に居て、その予測の相手と段が検知した当たりと同じ場合 true、それ以外の場合は false
    [[nodiscard]] bool ContinuesBeforeContact(const GL::Level::ImpactRecord& impact) const noexcept;
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
    // =========================================================================
    // 内部ヘルパー：チャージ観測・接地復帰・状態進行・アニメーション選択
    // =========================================================================
    //! @brief このフレームの押しを控え、押していれば狙いの線と狙う相手を探して控える
    //! @details 控えるのは m_chargeObserved だけで、判定・速度・丸まりには触らない
    //! @param[in] held 体当たりのボタンを押しているか
    void ObserveCharge(bool held);
    //! 観測済みの保持・狙いを確定し、溜めと発動要求を進める。dt は秒
    void AdvanceCharge(float dt);
    //! 溜めすぎの横揺れを狙いに足し、線上の相手を再検索する。dt は秒
    void ApplyChargeSway(float dt);
    //! @brief 溜めを捨てる。放した扱いにはしないので、通常突進も溜めた突進も出ない
    //! @details 判定と狙いの控えを初めの値へ戻し、最高速度の倍率を 1 へ戻す。
    //! 構えは判定から答えるので 1 に戻る。丸まりは解かず、着地で解ける
    void CancelCharge() noexcept;
#if !defined(NS_SHIPPING)
    //! 溜めている間、当たりの足元に溜め量で広がる輪を描く
    void DrawChargeRing() const;
#endif
    //! 着地で戻す物を戻す。跳べる回数を 1 にし、RestoreWhileGrounded も行う。普通の着地とよじ登りの終わりが呼ぶ
    void RestoreOnLanding() noexcept;
    //! 接地している間は毎フレーム、コヨーテの猶予と空中の突進の使い切りを戻す
    void RestoreWhileGrounded() noexcept;
    //! @brief ジャンプの縦の初速を与え、跳べる回数を 1 減らし、コヨーテと先行入力を使い切る
    //! @details 地面からの Jump と縁からの LedgeJump が呼ぶ。跳べるかは呼び手が決める
    void LaunchJump() noexcept;
    //! 突進の発動の判定を通れば突進を出す。状態機械を進める前に呼ぶ
    void PrepareStateStep();
    //! 状態機械を進めた後の控えの更新。丸まりを解く判定・長押しの控え・押下の消費・要求と狙いの経過を進める
    void FinishStateStep(float dt);
    //! @brief DecideStep の終わりに状態機械を進める支度をする
    //! @details 止めの間は何もしない。PrepareStateStep を呼んだら真を返し、AfterStateStep が FinishStateStep を呼ぶ
    [[nodiscard]] bool PrepareOrSkipStateStep();
    //! @brief 掴まり位置から掴める縁を探す
    //! @details 手を伸ばす向きは今掴んでいる面の向き。面が折れていれば、見つけた面の向きを返す
    //! @param[in] hangPos 手を伸ばす元になるカプセル中心の位置
    //! @param[out] outTop 見つけた縁の上端の y。見つからない場合は書き換えない
    //! @param[out] outFaceNormal 見つけた面の、自機の側を向く水平の向き。見つからない場合は書き換えない
    //! @param[out] outHangPos 見つけた面から半径だけ外のぶら下がりの中心。見つからない場合は書き換えない
    //! @return 手の高さの下と、手より上へ欄「掴まっている縁の上向きの許し」までの帯に縁があり、登り先も塞がって
    //! いない場合 true、それ以外の場合は false
    [[nodiscard]] bool FindLedgeTopAt(const NS::Vector3& hangPos,
                                      float& outTop,
                                      NS::Vector3& outFaceNormal,
                                      NS::Vector3& outHangPos) const noexcept;
    // 状態と水平の速さから役の欄を選ぶ。字面が空の役は立ちの欄に代える
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
    std::unique_ptr<GL::Player::ChargeEffects> m_chargeEffects;
    std::unique_ptr<GL::Player::ImpactEffects> m_impactEffects;
    GL::Level::TackleGhost m_tackleGhost; // 体当たりの残像。描く時に Model を渡されて描く
    // 残像の進み。当たりの状態に入ったフレームの見た目の段を 1 フレーム目に数え、止めの後も状態に依らず進める
    GL::Level::TackleGhostRun m_ghostRun;
    bool m_slowedWorld = false; // 紫の明けの束で世界を遅くしたか。演出を止める時に普段の速さへ戻す
    // 当たる直前の状態に入った予測。入る予約で書き、状態の OnEnter と検知のフレームが読む
    struct BeforeContactEntry
    {
        NS::SceneActor* target = nullptr;
        GL::Level::HitTier tier = GL::Level::HitTier::Center;
        int startFrame = 0;  // 状態の最初の歩に当てる動作のフレーム
        NS::Vector3 point{}; // 予測した玉が相手に触れる時の、玉の中心
    };
    BeforeContactEntry m_beforeContact;
    NS::StateMachineOf<PlayerActor>* m_states = nullptr; // 基底が所有する。OnInit が預けた直後から有効
    GL::Level::Health m_health;
    std::optional<NS::Vector3> m_courseStartPosition; // MsgCourseStart の時の位置。やり直しで戻る先

    // =========================================================================
    // メンバ変数：通常移動・ジャンプ・突進・反動・チャージ・崖掴まり状態
    // =========================================================================
    bool m_inputInjected = false;     // Update(bool) の間。機器を読まず、差し込んだ入力のまま進める
    bool m_stateStepPrepared = false; // DecideStep が PrepareStateStep を呼んだか。書くのは DecideStep だけ
    bool m_prevJumpHeld = false;
    int m_jumpsRemaining = 1;
    float m_coyoteTimer = 0.0f; // コヨーテ猶予の残り秒
    float m_bufferTimer = 0.0f; // 先行ジャンプ入力の残り秒
    // ダッシュと反動で始め、着地とやり直しで終える。経過は落下の状態に居る間だけ数える
    bool m_airControlReturning = false;
    float m_airControlReturnElapsed = 0.0f;

    float m_maxSpeedScale = 1.0f; // 走行速度に掛ける倍率。書くのは AdvanceCharge と CancelCharge

    GL::Player::BodySlamStatus m_slam;
    BodySlamRequest m_request;
    GL::Player::ReboundStatus m_rebound;
    GL::Player::SkidStatus m_skid;
    GL::Player::AirDashStatus m_airDash;
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

//! 実体のアクターからプレイヤーを引く。無ければ nullptr
//! @param[in,out] objects 探す先のアクター。返した PlayerActor* から中身が書き換わる
[[nodiscard]] PlayerActor* FindPlayer(NS::ActorList& objects) noexcept;
