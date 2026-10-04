#pragma once

#include "Game/Level/Health.h"
#include "Game/Level/ImpactInputJudge.h"
#include "Game/Level/SlamAim.h"
#include "Game/Player/PlayerEvents.h"
#include "Game/Player/ReboundArc.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Core/Sphere.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/CameraTarget.h"
#include "Runtime/Object/Scene/SceneJson.h"
#include "Runtime/Object/StateMachine.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace NS::Obj
{
    class Body;
    class Collider;
    class ObjectList;
    class PlayerInput;
} // namespace NS::Obj

namespace NS::Game::Player
{
    class PlayerParams;
    class PlayerAppearance;
    class ChargeEffects;
    class ImpactEffects;
} // namespace NS::Game::Player

namespace NS::Game::Level
{
    class ImpactResolver;
    class TargetMarker;
    class SlamArrow;
} // namespace NS::Game::Level

//! @brief プレイヤーキャラクタ。固定の部品をコードで組む
//! @details 部品と部品名は ForEachPart が正。Body が Movement、Collider が Collider、PlayerInput が Input を名乗る。
//! 値はプレイヤーの種類の既定値と個体の上書きから写す。
//! 状態機械と命は Actor 自身が持ち、入力の窓口・移動の組み立て・崖つかまり・突進と反発とそれらの記録はここが持つ。
//! 速度と接地の計算は身体の部品 (Body) へ、当たりの寸法と地形に当てて押し返す移動は Collider へ任せる。
//! 状態の遷移の条件は PlayerJudges の判定を状態が呼ぶ
//! 落下死やゴールは体のセンサーへ届く知らせで受け取り、コースの流れは進行役へ伝えるだけにする
//! 実装は 4 つに分ける。Player.cpp (生成・部品・更新の流れ・入力・記録)、
//! PlayerMovement.cpp (移動の組み立てと崖つかまり)、PlayerBodySlam.cpp (突進・反動・丸まり)、
//! PlayerCharge.cpp (溜めと狙い)
class Player : public NS::Obj::Actor, public NS::Obj::ICameraTarget
{
public:
    //! 既定の構成と見た目で組む。Mesh / Material は後からファクトリが入れる
    Player() noexcept;
    ~Player() override;
    void ForEachPart(const PartVisitor& visitor) const override;

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    Player(Player&&) = delete;
    Player& operator=(Player&&) = delete;

    //! 保存形式と TypeRegistry の登録名。読込はこの名前で Actor の型を選ぶ
    NS_REFLECT_NONE(Player, NS::Obj::Actor)
    //! 基底が所有する自機の状態機械。コンストラクタが組むので、作った直後から立ちの状態に居る
    [[nodiscard]] NS::Obj::StateMachine<Player>& States() noexcept { return *m_states; }
    [[nodiscard]] const NS::Obj::StateMachine<Player>& States() const noexcept { return *m_states; }
    [[nodiscard]] NS::Obj::PlayerInput& Input() noexcept { return *m_input; }
    [[nodiscard]] const NS::Obj::PlayerInput& Input() const noexcept { return *m_input; }
    //! 速度・接地を持つ身体の部品。部品名は保存の鍵なので "Movement" のまま
    [[nodiscard]] NS::Obj::Body& Body() noexcept { return *m_body; }
    [[nodiscard]] const NS::Obj::Body& Body() const noexcept { return *m_body; }
    //! 動く体の当たりの部品で、部品名は "Collider"。カプセルの寸法と、地形に当てて押し返す移動を持つ。
    //! 体の周りの地形はこれを渡して問う
    [[nodiscard]] NS::Obj::Collider& Collider() noexcept { return *m_collider; }
    [[nodiscard]] const NS::Obj::Collider& Collider() const noexcept { return *m_collider; }
    [[nodiscard]] NS::Game::Player::PlayerParams& Params() noexcept { return *m_params; }
    [[nodiscard]] const NS::Game::Player::PlayerParams& Params() const noexcept { return *m_params; }
    //! 溜めの判定を読むだけの口。溜め量・押しているか・溜めている間かは、見た目の部品と追従カメラがここから読む
    [[nodiscard]] const NS::Game::Level::ImpactInputJudge& ChargeJudge() const noexcept;
    //! @brief 押している間に控えた狙いの線を読む
    //! @details 押している間は毎フレーム、自機の位置から CameraForwardHorizontal の向きへ、
    //! BodySlamDistance の長さの線を控える。向きは効果を掛ける前の遊びの視点から作り、仮想カメラが無ければ +Z。
    //! 押したキーとスティックの向きは使わない。狙う相手がいなくても控える。
    //! 縦の速さは狙う相手の予測の値で、相手が無ければ 0。接地はその時の身体の値。
    //! 押していないフレーム、カメラの管理役が無いフレーム、向きが非数のフレームは控えが無い
    //! @param[out] outLine 控えた狙いの線。控えが無い場合は書き換えない
    //! @return 控えがある場合 true、それ以外の場合は false
    [[nodiscard]] bool TryGetAimLine(NS::Game::Level::AimLine& outLine) const noexcept;
    //! @brief 押している間に控えた狙う相手を読む
    //! @details 押している間は毎フレーム、狙いの線の向きと長さで ImpactResolver::FindSlamLineTarget を呼び、
    //! 線を進む自機の縁が突進が止まる所までに触れる相手を控える。
    //! 押していないフレームと、狙いの線が無いフレームは控えが無い
    //! @param[out] outTarget 控えた狙う相手。控えが無い場合は書き換えない
    //! @return 控えがある場合 true、それ以外の場合は false
    [[nodiscard]] bool TryGetAimTarget(NS::Game::Level::SlamLineTarget& outTarget) const noexcept;
    //! @brief 狙いの線 (紫の揺れを足した向き) を進んだ時に最初に触れる相手を読む
    //! @details 揺れていない間は TryGetAimTarget と同じ相手。紫の揺れで線が振れている間は、振れた線の向きで
    //! ImpactResolver::FindSlamLineTarget を引き直した相手で、矢印の先はこれで決まる。
    //! 狙う相手の枠と放つ縦の速さは揺れていない線の相手 (TryGetAimTarget) のまま
    //! @param[out] outTarget 線の上の相手。控えが無い場合は書き換えない
    //! @return 控えがある場合 true、それ以外の場合は false
    [[nodiscard]] bool TryGetLineTarget(NS::Game::Level::SlamLineTarget& outTarget) const noexcept;
    //! @brief 紫の揺れの位相 (ラジアン) を返す
    //! @details 溜めすぎのフレーム数から閉じた式で出す。紫になりきった瞬間に π/2 を通る。紫でない間は 0
    [[nodiscard]] float ChargeSwayPhase() const noexcept { return m_charge.swayPhase; }
    //! @brief 紫の揺れのずれを返す
    //! @details 狙いの線の右を正にした、相手の所 (相手がいなければ欄「相手がいない時に直す距離」の所) での横のずれ。
    //! 単位は m。紫でない間は 0
    [[nodiscard]] float ChargeSwayOffset() const noexcept { return m_charge.swayOffset; }
    //! @brief このフレームに紫の揺れが端を通った場合 true、それ以外の場合は false
    //! @details 位相が π/2 + nπ を通ったフレーム。紫になりきるフレームは必ず端を通る。紫に入ったフレームは数えない
    [[nodiscard]] bool ChargeSwayReachedEdge() const noexcept { return m_charge.swayReachedEdge; }
    //! @brief 構えで縦に縮める倍率を読む
    //! @details 溜めている間は欄「構えの縮み」、溜めに入る前に押している間は欄「押しの構えの縮み」、それ以外は 1。
    //! 描く形へ書くのは PlayerAppearance で、ここは問いに答えるだけ
    //! @return 元の形を 1 とした縦の倍率
    [[nodiscard]] float StanceHeight() const noexcept;
    [[nodiscard]] NS::Game::Level::ImpactResolver& Resolver() noexcept { return *m_resolver; }
    [[nodiscard]] NS::Game::Player::PlayerAppearance& Appearance() noexcept { return *m_appearance; }
    [[nodiscard]] NS::Game::Level::TargetMarker& TargetIndicator() noexcept { return *m_targetMarker; }
    [[nodiscard]] NS::Game::Level::SlamArrow& SlamIndicator() noexcept { return *m_slamArrow; }
    [[nodiscard]] NS::Game::Player::ChargeEffects& ChargeVisuals() noexcept { return *m_chargeEffects; }
    [[nodiscard]] NS::Game::Player::ImpactEffects& ImpactVisuals() noexcept { return *m_impactEffects; }

    [[nodiscard]] NS::Obj::UpdatePhase Phase() const noexcept override { return NS::Obj::UpdatePhase::Player; }
    void ReadInput() override;
    using NS::Obj::Actor::Update;
    //! @brief 体当たりのボタンの押下を差し込んで 1 固定ステップ進める。試しが入力を作る口
    //! @details chargeHeld を PlayerInput::SetSlamHeld で書いてから基底の Update を回す。
    //! 入力の段は回さないので、書いた押しがそのまま観測の段に届く
    //! @param[in] chargeHeld 体当たりのボタンを押しているか
    void Update(bool chargeHeld);
    //! @brief 状態と水平の速さから再生するクリップと速度を選び、同居する Animation へ渡す
    //! @details Animation が無ければ何もしない。選んだクリップが無ければ立ちのクリップへ戻す
    void UpdateAnimation();
    void OnEndPlay() override;

    // 入力の窓口。値は PlayerInput が持つ
    //! world 空間の目標移動方向と速度スケールを渡す。スケールは 0..1 に丸める
    void SetDesiredMove(const NS::Core::Vector3& worldDir, float speedScale01) noexcept;
    //! 直近に渡された目標速度スケール 0..1
    [[nodiscard]] float DesiredSpeedScale() const noexcept;
    //! 直近に渡された world 空間の目標移動方向。長さは入力の強さのままで正規化されていない
    [[nodiscard]] NS::Core::Vector3 DesiredDirection() const noexcept;
    //! 掴まり中の生ローカル入力で各成分は -1..1。SetDesiredMove とは別に持つ
    void SetClimbMove(float localRight, float localForward) noexcept;
    [[nodiscard]] float ClimbRight() const noexcept;
    [[nodiscard]] float ClimbForward() const noexcept;
    //! ジャンプの押下を 1 回ぶん立てる。更新の終わりに落ちるので次のフレームには残らない
    void SetJumpPressed() noexcept;
    //! 手放しの押下を 1 回ぶん立てる。更新の終わりに落ちるので次のフレームには残らない
    void SetReleaseLedgePressed() noexcept;
    //! ジャンプボタンの長押し状態を渡す。上昇中に離すと縦速度を縮める
    void SetJumpHeld(bool held) noexcept;

    // 移動の記録と読み取り
    [[nodiscard]] int JumpsRemaining() const noexcept { return m_jumpsRemaining; } //!< 残りジャンプ回数
    //! @brief 走行速度に SetMaxSpeedScale の倍率を掛けた最高速度。負になる場合は 0
    //! @details 負のまま返すと EndBodySlam の頭打ちが cap / speed で負の倍率になり、突進明けに水平の向きが反転する
    [[nodiscard]] float MaxSpeed() const noexcept;
    [[nodiscard]] float RunSpeed() const noexcept;
    //! 奈落落ちの復活などで速度・接地・ジャンプまわりの記録と状態機械を初期状態へ戻す。
    //! 丸まりも解くが根は動かさない。走っている当たりのタイムラインは ImpactResolver::CancelImpact で捨て、
    //! 描く形の倍率は PlayerAppearance::ResetDrawScale で補間なしに 1 へ戻す。
    //! 呼び手は先に根を出現位置へ置いてから呼ぶ
    void ResetState() noexcept;
    //! 着地でジャンプ回数を戻し、接地中はコヨーテ猶予と突進の使用済みを戻す
    void SyncGroundState() noexcept;
    //! 自機だけの通知の受け口。身体の Events() は接地の 2 件を返すので名前を分ける
    [[nodiscard]] NS::Game::Player::PlayerEvents& PlayerEventsRef() noexcept { return m_playerEvents; }

    // 突進・反発・丸まりの読み取り
    //! 突進中の場合 true、それ以外の場合は false
    [[nodiscard]] bool IsBodySlamming() const noexcept;
    //! 突進の進み具合 0..1。突進中でなければ 0
    [[nodiscard]] float BodySlamProgress01() const noexcept;
    [[nodiscard]] float BodySlamCharge01() const noexcept { return m_slam.charge01; } //!< 発動時の溜め量 0..1
    //! 発動時の溜めすぎの深さ 0..1。赤で放した突進と通常突進は 0
    [[nodiscard]] float BodySlamOvercharge01() const noexcept { return m_slam.overcharge01; }
    //! 溜めた突進を終える水平の距離。欄「突進距離」の値で、単位は m
    [[nodiscard]] float BodySlamDistance() const noexcept;
    //! @brief 最後に出した突進の水平の向きを返す
    //! @details 突進の間は向きが変わらない。突進が終わった後も、次の突進を出すまで残す
    //! @return 正規化済みの向き。突進を出す前と ResetState の後はゼロ
    [[nodiscard]] NS::Core::Vector3 BodySlamDirection() const noexcept { return m_slam.dir; }
    //! 反動の状態の場合 true、それ以外の場合は false
    [[nodiscard]] bool IsRebounding() const noexcept;
    //! 外れの反動の着地からこすって止まる状態の場合 true、それ以外の場合は false
    [[nodiscard]] bool IsSkidding() const noexcept;
    //! @brief 今の反動が着いた後にこすって止まる場合 true、それ以外の場合は false
    //! @details 外れの反動で、欄「外れのこすって止まるまでのフレーム数」が 1
    //! 以上の時に真。偽なら着いたフレームに立ちへ戻る
    [[nodiscard]] bool SkidsOnLanding() const noexcept;
    //! @brief 身体を今動かしてよい場合 true、それ以外の場合は false
    //! @details 身体の部品が外されておらず、当たりの止め (ImpactResolver::IsHitStopping) の最中でも、
    //! 止めの明けの後に反動の事象を待つ間 (ImpactResolver::IsAwaitingRebound) でもない時に真。
    //! 状態機械の 1 歩・身体の移動・玉の回転と着地の潰れの戻しがこの問いを読む
    [[nodiscard]] bool CanMoveBody() const noexcept;
    //! 最後に始めた反動の水平の向き。正規化済み。反動を始める前と ResetState の後はゼロ
    [[nodiscard]] NS::Core::Vector3 ReboundDirection() const noexcept { return m_rebound.direction; }
    //! @brief 突進の玉の回る速さ (度/秒) を返す
    //! @details 届くまでの回転数 × 360 ÷ 届くまでの秒。回転数は溜めた突進が欄「溜めた突進の届くまでの回転数」、
    //! 通常突進が欄「通常突進の届くまでの回転数」。届くまでの秒は突進を終える水平の距離 ÷ 突進の水平の速さ。
    //! 速さや距離を触っても、届くまでに回る数は変わらない
    //! @return 回る速さ。届くまでの秒が 0 以下か有限でない時は 0
    [[nodiscard]] float BodySlamSpinSpeed() const noexcept;
    //! @brief 最後に始めた反動の玉の回る速さ (度/秒) を返す
    //! @details 反動の回転数 × 360 ÷
    //! 発射の高さへ戻るまでの秒。回転数は溜めて当てた反動が欄「溜めて当てた反動の回転数」、
    //! 通常突進で当てた反動が欄「通常突進で当てた反動の回転数」。書くのは BeginRebound
    //! @return 回る速さ。反動を始める前と ResetState の後は 0
    [[nodiscard]] float ReboundSpinSpeed() const noexcept { return m_rebound.spinSpeed; }
    //! @brief 最後に始めた反動の外れの回り方を返す
    //! @details 外れの反動の時だけ値を持つ。書くのは BeginRebound
    [[nodiscard]] const std::optional<NS::Game::Player::MissTumble>& ReboundMissTumble() const noexcept
    {
        return m_rebound.missTumble;
    }
    //! @brief 反動を始めた回数を返す
    //! @details 見た目が新しい反動の始まりを知るために読む。ResetState では戻さない
    [[nodiscard]] std::uint32_t ReboundCount() const noexcept { return m_rebound.count; }
    //! @brief こすって止まる間の、着いた水平の速さに掛けている今の倍率を返す
    //! @return 0〜1。こすって止まる状態でない間は 1
    [[nodiscard]] float SkidSpeedScale() const noexcept;
    //! 丸まっている場合 true、それ以外の場合は false
    [[nodiscard]] bool IsCurled() const noexcept { return m_curled; }
    //! @brief 根を rootPosition に置いた時の突進の玉を返す
    //! @details 玉は今の当たりのカプセルの下の球 (中心は根から半分の高さだけ下、半径は同じ)。丸まる時は下端を
    //! 揃えて根を下げるので、立ち姿の下の球が丸まった後の玉になる。丸まっていれば半分の高さが 0 で、玉の中心は根
    //! そのもの。判定・矢印・エディタの面は玉をここから引く。丸まりの揃え方を変える時は ChangeCurled と一緒に直す
    //! @param[in] rootPosition 根の位置 (ワールド)
    [[nodiscard]] NS::Core::Sphere SlamBallAt(const NS::Core::Vector3& rootPosition) const noexcept;

    // 移動の組み立て。状態が呼ぶ順序がそのまま手触りになる。速度・接地・重力の計算は身体 (Body) が持ち、
    // どの状態でどの値をどの順に掛けるかをここが決める。調整値は PlayerParams から読む
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
    //! @brief 反動の間の重力を当てる
    //! @details 上向きの間は上昇重力に反動の上りの重力倍率を掛け、頂点の近くはさらに頂点滞空倍率を掛ける。
    //! 上向きでなければ Gravity と同じ。組は PlayerParams::ReboundGravity、選び方は Gravity と同じ ChooseGravity
    void ReboundGravity(float dt) noexcept;
    //! @brief 反動の間、入力の向きへ反動中の空中の加速度で加速する。入力が無ければ何もしない
    //! @details 接地の印に依らずこの加速度を使い、入力の向きからずれた速度は減らさない
    void AccelerateDuringRebound(float dt) noexcept;
    //! 外れの着地からこすって止まり始める。今の水平の速度を着いた速度として控え、経過を 0 にする
    void BeginSkid() noexcept;
    //! @brief こすって止まる 1 フレームを進める
    //! @details 経過を 1 進め、水平の速度を 着いた速度 × MissSkidSpeedScale (経過, 欄「外れのこすって止まるまでの
    //! フレーム数」, 欄「外れのこすって止まる減り方」) に書く
    //! @return 止まりきった場合 true、それ以外の場合は false
    [[nodiscard]] bool AdvanceSkid() noexcept;
    //! @brief 突進の 1 フレームを進める。溜めた突進は水平を BodySlamVelocity で書き直し、重力を当てる
    //! @details 突進の間の水平の書き手はここだけ
    void UpdateBodySlam(float dt) noexcept;

    // 崖つかまり。縁の検出・つかまり・登り
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
    //! よじ登りの 1 フレーム。終われば通常移動へ戻す
    void UpdateLedgeClimb(float dt) noexcept;
    //! 手を放し、その場から落下させる
    void DropLedge() noexcept;

    // 突進と反発。速度と接地は身体が持つ
    //! @brief 体当たりの発動を要求する
    //! @details 溜め量 0 は通常突進で、非有限値は 0 とみなす。
    //! そのフレームで出せない要求は先行入力時間だけ覚え、過ぎたら失効する。
    //! 突進中と、身体を動かせない間 (当たりの止めと、止めの明けの後に反動を待つ間) の要求は覚えずに捨てる。
    //! 空中で出すと接地するまで次は出せない。地面から出した突進は数えないので、その後の空中で 1 回出せる。
    //! 出る向きは BodySlam が入力と押したフレームの控えから決める。前の要求に添えた向きは捨てる
    //! @param[in] charge01 溜め量 0..1
    //! @param[in] overcharge01 溜めすぎの深さ 0..1。威力を溜めきりより上げる。有限でなければ 0
    void RequestBodySlam(float charge01, float overcharge01 = 0.0f) noexcept;
    //! @brief 出す向きを添えて体当たりの発動を要求する
    //! @details 溜め量と先行入力と捨てる時は 1 つ引数の RequestBodySlam と同じ。捨てた時は向きも覚えない。
    //! 出る時は入力と押したフレームの控えを見ず、添えた向きの水平を正規化した向きへ出す。
    //! 水平の長さが 0 の向きと有限でない向きは、添えなかったのと同じ
    //! @param[in] charge01 溜め量 0..1
    //! @param[in] aimDirection 出す向き。世界座標で、縦の成分は使わない
    //! @param[in] launchVerticalSpeed 溜めた突進を放つ瞬間の縦の速さ (m/s)。上が正。溜めの観測が狙う相手の予測
    //! (SlamLineTarget::launchVerticalSpeed) から控えた値で、届く相手が無ければ 0。通常突進には効かない。
    //! 有限でなければ 0
    //! @param[in] overcharge01 溜めすぎの深さ 0..1。威力を溜めきりより上げる。有限でなければ 0
    void RequestBodySlam(float charge01,
                         const NS::Core::Vector3& aimDirection,
                         float launchVerticalSpeed = 0.0f,
                         float overcharge01 = 0.0f) noexcept;
    //! @brief 突進の速度を返す。突進中は発動時の向きと BodySlamSpeed から作り、縦は身体の今の値
    //! @details 衝突の裁定と玉の回転が読み、溜めた突進の間は UpdateBodySlam がこの水平で身体を書き直す。
    //! 実速度は壁へ押し付けられたフレームで 0 に潰れ、衝突の先読みが今の位置から動かなくなる
    //! @return 突進中は突進の速度、それ以外は身体の速度
    [[nodiscard]] NS::Core::Vector3 BodySlamVelocity() const noexcept;
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
    [[nodiscard]] NS::Core::Vector3 AimDirection() const noexcept;
    //! @brief 速度を ReboundVelocityFor の値にして反動の状態へ移す
    //! @details 反動の間は上りの重力に反動の上りの重力倍率を掛け、下りは普段の重力のまま
    //! 反動の間は跳べず、空中の操作は反動中の空中の加速度だけ効く
    //! 接地していて上向きの速度が無くなったフレームに立ちへ移る
    //! @param[in] arc 弾かれる向きと頂点の高さと横の距離
    //! @return 反動を始めた場合 true、ReboundVelocityFor が 0 を返す arc で何も変えなかった場合は false
    [[nodiscard]] bool BeginRebound(const NS::Game::Player::ReboundArc& arc) noexcept;
    //! @brief arc の反動を始める瞬間の速度 (m/s) を返す
    //! @details 飛ばした物の曲線と同じ式 LaunchArcInitialVelocity で出す。
    //! 上りの重力は上昇重力 × 反動の上りの重力倍率、下りは下降重力、頂点の帯は頂点滞空 Vy と頂点滞空倍率
    //! @param[in] arc 弾かれる向きと頂点の高さと横の距離
    //! @return 反動の初速。高さか距離が有限の正でない時、向きに水平の成分が無い時、
    //! 重力の欄から曲線が組めない時は 0
    [[nodiscard]] NS::Core::Vector3 ReboundVelocityFor(const NS::Game::Player::ReboundArc& arc) const noexcept;
    //! @brief 丸まりを入れるか解く
    //! @details 押している間は毎フレーム true が入る。自分で解くので、false はプレイを終える時だけ渡す。
    //! 丸まると当たりを球にして根を立ち姿の半長ぶん下げ、解くと立ち姿へ戻して上げる。
    //! 縁に掴まっている間とよじ登っている間の true は受けない
    void SetCurled(bool curled) noexcept;

    //! 追従カメラに追われる時の窓口。自分の状態を自分で答える
    [[nodiscard]] const NS::Obj::ICameraTarget* GetCameraTarget() const noexcept override { return this; }

    //! 接地・速度・見る高さ・反動・溜めの状態を自分の部品から組む
    [[nodiscard]] NS::Obj::CameraTargetState GetCameraTargetState() const override;

    //! コースの進行役を用意する。全ての配置物が揃った後に呼ばれる
    void InitAfterPlacement() override;

    //! @brief 即死・ゴール・コースのやり直し・操作の停止の知らせに応じる
    //! @details 操作の停止は PlayerInput の止め (SetLocked) へ写し、止める時は CancelCharge で溜めを捨てる
    bool ReceiveMsg(const NS::Obj::Message& msg, NS::Obj::HitSensor* sender, NS::Obj::HitSensor* receiver) override;

    //! @brief プレイ開始時の凍結 (baseline) の自分の位置へ戻り、動きと命を最初の状態へ戻す
    //! @details 凍結に自分が居なければ、DefaultSpawnPosition の位置へ戻す
    void RestartFrom(const nlohmann::json& baseline) noexcept;

    //! 命を amount 削る。下限 0
    void ApplyDamage(int amount) noexcept;

    //! @brief 即死する。命を 0 にして世界から消える
    //! @details 配置を止めるだけの Kill / SetActive(false) は命に触らない。死は落下死などの知らせだけが起こす
    void Die() noexcept;

    //! @brief 命を満タンへ戻す。プレイ突入とリスタートで呼ばれる
    //! @details 満タンは呼ぶたびに調整値の「体力」から読む。エディタで書き換えた値が次の再生から効く
    void ResetHealth() noexcept;

    [[nodiscard]] bool IsDead() const noexcept;
    [[nodiscard]] int Health() const noexcept;

protected:
    //! PlayerInput の体当たりの押しと狙いの観測 (ObserveCharge)、
    //! 体当たりの衝突の観測 (ImpactResolver::ObserveImpact)。副作用は無い
    void ObserveStep() override;
    //! @brief 溜めを進め (AdvanceCharge)、衝突の裁定を出して知らせを送る (ImpactResolver::StepState)
    //! @details 裁定役が外されていれば、走っている当たりのタイムラインを捨てる (ImpactResolver::CancelImpact)
    void DecideStep() override;
    //! 突進の発動、状態機械の 1 歩、丸まりの解除と押下の消費。CanMoveBody が偽の間は押下の消費だけ
    void StateStep() override;
    //! CanMoveBody が真なら身体を動かす
    void BodyStep() override;
    //! クリップの選択、TargetMarker、SlamArrow、HitReaction、PlayerAppearance、ChargeEffects、ImpactEffects の順。
    //! 開発用のビルドは最後に溜めの輪を描く
    void VisualStep() override;

private:
    //! 突進の記録。発動で書き、突進の間と後で読む
    //! @details 突進の状態へは持たせない。dir と charge01 は突進が終わった後も読まれる。ResetState
    //! で全部を初期値へ戻す
    struct BodySlamRecord
    {
        float travelled = 0.0f;      // 突進で進んだ水平距離
        float distanceTarget = 0.0f; // 突進を終える水平距離
        bool isTap = false;          // 溜め量 0 の飛び込みか
        bool justStarted = false;    // 発動したフレームか
        NS::Core::Vector3 dir{};     // 最後に出した突進の水平の向き。正規化済み。書くのは出せた時だけ
        float charge01 = 0.0f;       // 発動時に確定した溜め量 0..1
        float overcharge01 = 0.0f;   // 発動時に確定した溜めすぎの深さ 0..1
        bool forced = false;         // 溜めすぎで勝手に出た突進か
        bool wasSlamming = false;    // 直前のフレームを突進中で終えたか。書くのは MoveBody と ResetState
    };

    //! 体当たりの要求と、押したフレームの狙いの控え
    struct BodySlamRequest
    {
        float bufferRemaining = 0.0f; // 出せないフレームの押しを覚える残り秒
        bool spent = false;           // 空中で発動してから接地していないか
        float charge01 = 0.0f;        // 要求された溜め量 0..1
        float overcharge01 = 0.0f;    // 要求された溜めすぎの深さ 0..1
        NS::Core::Vector3 dir{};      // 要求に添えた出す向き。正規化済み
        bool hasDir = false;          // 要求に向きが添えてあるか
        float verticalSpeed = 0.0f;   // 溜めた突進を放つ瞬間の縦の速さ。hasDir が偽の間は読まない
        NS::Core::Vector3 aimDir{};   // 押したフレームに控えた狙いの向き。正規化済み
        float aimAge = 0.0f;          // 狙いを控えてからの経過秒
    };

    //! 反動の記録
    struct ReboundRecord
    {
        NS::Core::Vector3 direction{};                            // 最後に始めた反動の水平の向き。正規化済み
        float spinSpeed = 0.0f;                                   // 最後に始めた反動の玉の回る速さ (度/秒)
        std::optional<NS::Game::Player::MissTumble> missTumble{}; // 最後に始めた反動の外れの回り方
        std::uint32_t count = 0;                                  // 反動を始めた回数
    };

    //! 外れの着地からこすって止まる間の記録。書くのは BeginSkid と AdvanceSkid
    struct SkidRecord
    {
        NS::Core::Vector3 landingVelocity{}; // 着いたフレームの水平の速度
        int elapsedSteps = 0;                // 着いてからのフレーム数
    };

    //! @brief 溜めと狙いの記録。観測の段 (ObserveCharge) が observed の側を書き、決定の段 (AdvanceCharge) が確定する
    //! @details 読み手が見るのは確定した側だけ。CancelCharge で全部を初期値へ戻す
    struct ChargeRecord
    {
        NS::Game::Level::ImpactInputJudge judge{};           // 通常突進と溜めの判定
        bool observedHeld = false;                           // 観測の段で読んだ体当たりの押し
        NS::Game::Level::AimLine observedAimLine{};          // 観測の段で引いた狙いの線
        bool observedHasAimLine = false;                     // 観測の段で狙いの線を引けたか
        NS::Game::Level::SlamLineTarget observedAimTarget{}; // 観測の段で見つけた狙う相手
        bool observedHasAimTarget = false;                   // 観測の段で狙う相手が見つかったか
        // 押している間の狙いの線。hasAimLine が偽の間は読まない
        NS::Game::Level::AimLine aimLine{};
        bool hasAimLine = false;
        // 押している間の狙う相手。hasAimTarget が偽の間は読まない
        NS::Game::Level::SlamLineTarget aimTarget{};
        bool hasAimTarget = false;
        // 紫の揺れを足した狙いの線で最初に触れる相手。hasLineTarget が偽の間は読まない
        NS::Game::Level::SlamLineTarget lineTarget{};
        bool hasLineTarget = false;
        float swayPhase = 0.0f;       // 紫の揺れの位相 (ラジアン)
        float swayOffset = 0.0f;      // 紫の揺れの横のずれ (m)。狙いの線の右が正
        int swayEdge = 0;             // 位相が通った端の番号。(位相 − π/2) ÷ π の切り捨て
        bool hasSwayEdge = false;     // 前のフレームの端の番号を控えているか
        bool swayReachedEdge = false; // このフレームに端を通ったか
    };

    //! @brief 身体を 1 フレーム動かす。更新の中で 1 回だけ呼ぶ
    //! @details 向きを回すのは動かす前で、接地の反映と突進の距離はその後。
    //! 状態の側で動かすと、状態を足した時に呼び忘れてもビルドが通り、その状態の間だけ動かなくなる
    void MoveBody(float dt) noexcept;
    //! 稼働していないフレームでも、そのフレーム限りの入力は落とす。残すと再開した時に古い押下が効く
    void SkipBodyStep() noexcept;
    //! 突進の進んだ距離を足し、距離を使い切るか進めなくなったら突進を終える
    //! @details 進めた距離は動かした後にしか出ないので、打ち切りの判定は状態でなくここに置く
    //! 受け取るのは直前の Move で実際に動いた量
    void AdvanceBodySlamTravel(const NS::Core::Vector3& delta) noexcept;
    //! 突進を終える。水平の速さを MaxSpeed で切り、接地していれば走りへ、空中なら落下へ移す
    void EndBodySlam() noexcept;
    //! 控えた狙いを今の向きにどれだけ混ぜるか 0..1。巻き戻し秒までは 1、消える秒で 0
    [[nodiscard]] float BodySlamAimBlend01() const noexcept;
    //! @brief 突進の水平の速さ (m/s) を返す
    //! @details 発動の初速 (BodySlam) と突進の速度 (BodySlamVelocity) がこれを読む。速さの式はここ 1 か所
    //! @return 通常突進は欄「通常突進の初速」、溜めた突進は欄「突進速度」
    [[nodiscard]] float BodySlamSpeed() const noexcept;
    //! @brief 体当たりの要求を覚えてよい場合 true、それ以外の場合は false
    //! @details 突進中と CanMoveBody が偽の間は偽。ここで覚えた押しは止めの間に減らず、明けや空振りの後に 2 本目になる
    [[nodiscard]] bool AcceptsBodySlamRequest() const noexcept;
    //! @brief 丸まりを入れるか解き、当たりの形と根の高さを一緒に切り替える
    //! @details 丸まると当たりを球にして根を立ち姿の半長ぶん下げる。
    //! 解くと立ち姿へ戻して、その時の立ち姿の半長ぶん上げる。当たりの下端 (中心 − 半長 − 半径) は動かない。
    //! 根は前フレームの位置と一緒にずらすので、描画の補間に動きとして映らない。今と同じ値なら何もしない。
    //! この揃え方が突進の玉の中心の決まり (SlamBallAt) を成り立たせている
    void ChangeCurled(bool curled) noexcept;
    //! @brief 丸まりを解く
    //! @details 押されていない・突進中でない・直前のフレームを突進中で終えていない・突進の予約が無い・
    //! 接地している・上向きの速度が無い、が揃ったフレームに解く。縁を掴んだ時に解くのは Player::LedgeGrab
    void UncurlWhenSettled() noexcept;
    //! 走行速度に掛ける倍率を渡す。非有限値は無視して直前の値を残す
    void SetMaxSpeedScale(float scale) noexcept;
    //! 押したフレームの狙いを控える。離すまでの遅れのぶん、向きを添えない発動はこの向きから始める
    void MarkBodySlamAim() noexcept;
    //! 体当たりのボタンを押しているかを渡す。押している間は丸まりを解かない
    void SetBodySlamHeld(bool held) noexcept;
    //! @brief このフレームの押しを控え、押していれば狙いの線と狙う相手を探して控える
    //! @details 控えるのは ChargeRecord の observed の側だけで、判定・速度・丸まりには触らない
    //! @param[in] held 体当たりのボタンを押しているか
    void ObserveCharge(bool held);
    //! @brief 観測の段で控えた押しで溜めを 1 フレーム進める
    //! @details 溜めを進める呼び手は決定の段のこの 1 か所。押したフレームの狙いの控え・丸まり・押しの印・
    //! 突進の要求・溜めの間の最高速度の倍率・溜めに入ったフレームの横の停止を書き、観測した狙いを確定する
    //! @param[in] dt 進める秒
    void AdvanceCharge(float dt);
    //! @brief 紫の揺れを確定した狙いの線へ足す
    //! @details 溜めすぎのフレーム数から位相とずれを閉じた式で出し、狙いの線の向きを左右へ回して、
    //! 回した線で最初に触れる相手を引き直す。紫でない間と線が無い間は揺らさない
    //! @param[in] dt 1 フレームの秒
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
    [[nodiscard]] bool FindLedgeTopAt(const NS::Core::Vector3& hangPos, float& outTop) const noexcept;
    [[nodiscard]] std::string_view ChooseClip(float lateralSpeed) const noexcept;
    [[nodiscard]] float ChoosePlaybackSpeed(std::string_view clip, float lateralSpeed) const noexcept;
    std::unique_ptr<NS::Game::Player::PlayerParams> m_params;
    std::string m_appliedClip{};
    std::unique_ptr<NS::Obj::PlayerInput> m_input;
    std::unique_ptr<NS::Obj::Body> m_body;
    std::unique_ptr<NS::Obj::Collider> m_collider;
    std::unique_ptr<NS::Game::Player::PlayerAppearance> m_appearance;
    std::unique_ptr<NS::Game::Level::ImpactResolver> m_resolver;
    std::unique_ptr<NS::Game::Level::TargetMarker> m_targetMarker;
    std::unique_ptr<NS::Game::Level::SlamArrow> m_slamArrow;
    std::unique_ptr<NS::Game::Player::ChargeEffects> m_chargeEffects;
    std::unique_ptr<NS::Game::Player::ImpactEffects> m_impactEffects;
    NS::Obj::StateMachine<Player>* m_states = nullptr; // 基底が所有する。コンストラクタが預けた直後から有効
    NS::Game::Level::Health m_health;

    bool m_prevJumpHeld = false; // 前のフレームの長押し状態
    int m_jumpsRemaining = 1;    // 残りジャンプ回数
    float m_coyoteTimer = 0.0f;  // コヨーテ猶予の残り秒
    float m_bufferTimer = 0.0f;  // 先行ジャンプ入力の残り秒

    float m_maxSpeedScale = 1.0f; // 走行速度に掛ける倍率。書くのは AdvanceCharge と CancelCharge

    // 丸まっているか。入れるのは AdvanceCharge と突進の発動、解くのは UncurlWhenSettled と縁を掴んだ時と
    // ResetState と EndCharge
    bool m_curled = false;
    // 体当たりのボタンを押しているか。書くのは AdvanceCharge・CancelCharge・ResetState
    bool m_bodySlamHeld = false;

    BodySlamRecord m_slam;
    BodySlamRequest m_request;
    ReboundRecord m_rebound;
    SkidRecord m_skid;
    ChargeRecord m_charge;
    // 紫に入った回数。紫になりきった時に揺れが来る端を毎回入れ替える。溜めを捨てても戻さない
    int m_overchargeCount = 0;

    NS::Core::Vector3 m_facingDir{0.0f, 0.0f, 0.0f};       // 掴む向き。動こうとした水平の向きへ振り向きの速さで回る
    float m_lastMoveDistance = 0.0f;                       // 直前の Move で動いた距離。縁を探す帯の上の余白
    float m_ledgeTopY = 0.0f;                              // 掴んでいる縁の上端の y
    NS::Core::Vector3 m_ledgeFaceNormal{0.0f, 0.0f, 0.0f}; // 掴んでいる面の外向き法線
    NS::Core::Vector3 m_ledgeMantleStart{0.0f, 0.0f, 0.0f};
    NS::Core::Vector3 m_ledgeMantleEnd{0.0f, 0.0f, 0.0f};
    float m_ledgeMantleTimer = 0.0f; // よじ登りの経過秒

    NS::Game::Player::PlayerEvents m_playerEvents;
};

//! live の配置物からプレイヤーを引く。無ければ nullptr
//! @param[in,out] objects 探す先の配置物。返した Player* から中身が書き換わる
[[nodiscard]] Player* FindPlayer(NS::Obj::ObjectList& objects) noexcept;

//! @brief プレイヤーの居ないレベルへ補う時と、凍結に自機が居ないやり直しで使う出現位置の既定
//! @details 水平は原点。高さは仮定した床の上面に、カプセルの半分の高さと半径と余白を足した中心の高さ。
//! 寸法は collider の欄から引く。
//! 補う側 (エディタの EnsurePlayerObject) とやり直しの落ち先 (RestartFrom) が同じこの関数を読む
//! @param[in] collider 立たせる自機の動く体の当たり。カプセルの寸法の持ち主
//! @return カプセルの中心の world 位置
[[nodiscard]] NS::Core::Vector3 DefaultSpawnPosition(const NS::Obj::Collider& collider) noexcept;
