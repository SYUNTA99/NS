#pragma once

#include "Game/Level/Health.h"
#include "Game/Player/PlayerEvents.h"
#include "Game/Player/ReboundArc.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/CameraTarget.h"
#include "Runtime/Object/Scene/SceneJson.h"
#include "Runtime/Object/StateMachine.h"

#include <string>
#include <string_view>

namespace NS::Obj
{
    class Body;
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
    class CollisionInput;
    class ImpactResolver;
    class TargetMarker;
    class SlamArrow;
} // namespace NS::Game::Level

//! @brief プレイヤーキャラクタ。Model / Body / PlayerInput / Shadow の既定構成をコードで組む
//! @details 部品名は Body が Movement、PlayerInput が Input。値はプレイヤーの種類の既定値と個体の上書きから写す。
//! 状態機械と命は Actor 自身が持ち、入力の窓口・移動の組み立て・崖つかまり・突進と反発とそれらの記録はここが持つ。
//! 速度と接地の計算だけは身体の部品 (Body) へ任せる。状態の遷移の条件は PlayerJudges の判定を状態が呼ぶ
//! 落下死やゴールは体のセンサーへ届く知らせで受け取り、コースの流れは進行役へ伝えるだけにする
//! 実装は 3 つに分ける。Player.cpp (生成・部品・更新の流れ・入力・記録)、
//! PlayerMovement.cpp (移動の組み立てと崖つかまり)、PlayerBodySlam.cpp (突進・反動・丸まり)
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
    //! 速度・接地・カプセル寸法・移動を持つ身体の部品。部品名は保存の鍵なので "Movement" のまま
    [[nodiscard]] NS::Obj::Body& Body() noexcept { return *m_body; }
    [[nodiscard]] const NS::Obj::Body& Body() const noexcept { return *m_body; }
    [[nodiscard]] NS::Game::Player::PlayerParams& Params() noexcept { return *m_params; }
    [[nodiscard]] const NS::Game::Player::PlayerParams& Params() const noexcept { return *m_params; }
    [[nodiscard]] NS::Game::Level::CollisionInput& ChargeControl() noexcept { return *m_collisionInput; }
    [[nodiscard]] const NS::Game::Level::CollisionInput& ChargeControl() const noexcept { return *m_collisionInput; }
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
    //! @details 順は基底の Update と同じで、観測の段が入力の読み取りの代わりに chargeHeld を使う
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
    //! 走行速度に掛ける倍率を渡す。非有限値は無視して直前の値を残す
    void SetMaxSpeedScale(float scale) noexcept;
    [[nodiscard]] float RunSpeed() const noexcept;
    //! 奈落落ちの復活などで速度・接地・ジャンプまわりの記録と状態機械を初期状態へ戻す。
    //! 丸まりも解くが根は動かさない。呼び手は先に根を出現位置へ置いてから呼ぶ
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
    //! 溜めた突進を終える水平の距離。欄「突進距離」の値で、単位は m
    [[nodiscard]] float BodySlamDistance() const noexcept;
    //! @brief 最後に出した突進の水平の向きを返す
    //! @details 突進の間は向きが変わらない。突進が終わった後も、次の突進を出すまで残す
    //! @return 正規化済みの向き。突進を出す前と ResetState の後はゼロ
    [[nodiscard]] NS::Core::Vector3 BodySlamDirection() const noexcept { return m_slam.dir; }
    //! 反動の状態の場合 true、それ以外の場合は false
    [[nodiscard]] bool IsRebounding() const noexcept;
    //! 最後に始めた反動の水平の向き。正規化済み。反動を始める前と ResetState の後はゼロ
    [[nodiscard]] NS::Core::Vector3 ReboundDirection() const noexcept { return m_rebound.direction; }
    //! 丸まっている場合 true、それ以外の場合は false
    [[nodiscard]] bool IsCurled() const noexcept { return m_curled; }

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
    //! タップの飛び込みだけに当てる重力。滞空秒がタップ距離を進む秒と揃う強さにする
    void TapSlamGravity(float dt) noexcept;
    //! @brief 反動の間の重力を当てる
    //! @details 上向きの間は上昇重力に反動の上りの重力倍率を掛け、頂点の近くはさらに頂点滞空倍率を掛ける。
    //! 上向きでなければ Gravity と同じ
    void ReboundGravity(float dt) noexcept;
    //! @brief 反動の間、入力の向きへ反動中の空中の加速度で加速する。入力が無ければ何もしない
    //! @details 接地の印に依らずこの加速度を使い、入力の向きからずれた速度は減らさない
    void AccelerateDuringRebound(float dt) noexcept;
    //! 突進の 1 フレームを進める。溜めた突進は水平を発動時の向きと速さで書き直し、重力を当てる
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
    //! @details 溜め量 0 はタップの飛び込みで、非有限値は 0 とみなす。
    //! そのフレームで出せない要求は先行入力時間だけ覚え、過ぎたら失効する。
    //! 1 度出すと接地するまで次は出せない。
    //! 出る向きは BodySlam が入力と押したフレームの控えから決める。前の要求に添えた向きは捨てる
    //! @param[in] charge01 溜め量 0..1
    void RequestBodySlam(float charge01) noexcept;
    //! @brief 出す向きを添えて体当たりの発動を要求する
    //! @details 溜め量と先行入力は 1 つ引数の RequestBodySlam と同じ。
    //! 出る時は入力と押したフレームの控えを見ず、添えた向きの水平を正規化した向きへ出す。
    //! 水平の長さが 0 の向きと有限でない向きは、添えなかったのと同じ
    //! @param[in] charge01 溜め量 0..1
    //! @param[in] aimDirection 出す向き。世界座標で、縦の成分は使わない
    //! @param[in] launchVerticalSpeed 溜めた突進を放つ瞬間の縦の速さ (m/s)。上が正。CollisionInput が狙いの段で
    //! LaunchPitch から控えた値で、届く相手が無ければ 0。タップには効かない。有限でなければ 0
    void RequestBodySlam(float charge01,
                         const NS::Core::Vector3& aimDirection,
                         float launchVerticalSpeed = 0.0f) noexcept;
    //! @brief 衝突の裁定と玉の回転が読む速度。突進中は向きと突進速度から作る
    //! @details 実速度は壁へ押し付けられたフレームで 0 に潰れ、衝突の先読みが今の位置から動かなくなる
    [[nodiscard]] NS::Core::Vector3 BodySlamVelocity() const noexcept;
    //! 突進の向きと突進速度で、水平の速度を書き直す。溜めた突進の間だけ効く
    void ApplyBodySlamHeading() noexcept;
    //! 突進を打ち切って通常移動へ戻す。突進中でなければ何もしない
    void CancelBodySlam() noexcept;
    //! @brief 向きを解決して突進を始める
    //! @details 向きは要求に添えた向き。添えていなければ AimDirection の向きに、押したフレームの控え
    //! (MarkBodySlamAim) を控えてからの秒に応じて混ぜる。
    //! 溜めた突進の縦の速さは要求に添えた値で、向きを添えていなければ 0。ジャンプの途中の縦の速さは持ち越さない。
    //! タップは欄「タップの上向き初速」
    //! @return 向きが決まらないか距離が 0 以下の場合 false、それ以外の場合は true
    [[nodiscard]] bool BodySlam() noexcept;
    //! 押したフレームの狙いを控える。離すまでの遅れのぶん、向きを添えない発動はこの向きから始める
    void MarkBodySlamAim() noexcept;
    //! @brief 向きを添えずに要求した体当たり (タップ) を出す水平の向きを返す
    //! @details 入力・カメラの前・速度の順に見て、どれも無ければゼロ。
    //! 溜めて放した突進は、CollisionInput が狙いの線を控えていればその向きを添えるので、この向きへは出ない
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
    //! 体当たりのボタンを押しているかを渡す。押している間は丸まりを解かない
    void SetBodySlamHeld(bool held) noexcept;

    //! 追従カメラに追われる時の窓口。自分の状態を自分で答える
    [[nodiscard]] const NS::Obj::ICameraTarget* GetCameraTarget() const noexcept override { return this; }

    //! 接地・速度・見る高さ・反動・溜めの状態を自分の部品から組む
    [[nodiscard]] NS::Obj::CameraTargetState GetCameraTargetState() const override;

    //! コースの進行役を用意する。全ての配置物が揃った後に呼ばれる
    void InitAfterPlacement() override;

    //! 即死・ゴール・コースのやり直し・操作の停止の知らせに応じる
    bool ReceiveMsg(const NS::Obj::Message& msg, NS::Obj::HitSensor* sender, NS::Obj::HitSensor* receiver) override;

    //! @brief プレイ開始時の凍結 (baseline) の自分の位置へ戻り、動きと命を最初の状態へ戻す
    //! @details 凍結に自分が居なければ、新規レベルで置く位置へ戻す
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
    //! Model の控え、溜めの判定 (CollisionInput::Observe)、体当たりの衝突の観測
    //! (ImpactResolver::ObserveImpact)。副作用は無い
    void ObserveStep() override;
    //! 溜めを進め (CollisionInput::AdvanceState)、衝突の裁定を出して知らせを送る (ImpactResolver::StepState)
    void DecideStep() override;
    //! 突進の発動、状態機械の 1 歩、丸まりの解除と押下の消費。身体が止まっている間は押下の消費だけ
    void StateStep() override;
    //! 突進の水平の速度の書き直し (CollisionInput::ApplyControl) の後に身体を動かす
    void BodyStep() override;
    //! クリップの選択、TargetMarker、SlamArrow、HitReaction、PlayerAppearance、ChargeEffects、ImpactEffects の順
    void VisualStep() override;

private:
    friend class NS::Game::Level::CollisionInput;
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
        bool wasSlamming = false;    // 直前のフレームを突進中で終えたか。書くのは MoveBody と ResetState
    };

    //! 体当たりの要求と、押したフレームの狙いの控え
    struct BodySlamRequest
    {
        float bufferRemaining = 0.0f; // 出せないフレームの押しを覚える残り秒
        bool spent = false;           // 発動してから接地していないか
        float charge01 = 0.0f;        // 要求された溜め量 0..1
        NS::Core::Vector3 dir{};      // 要求に添えた出す向き。正規化済み
        bool hasDir = false;          // 要求に向きが添えてあるか
        float verticalSpeed = 0.0f;   // 溜めた突進を放つ瞬間の縦の速さ。hasDir が偽の間は読まない
        NS::Core::Vector3 aimDir{};   // 押したフレームに控えた狙いの向き。正規化済み
        float aimAge = 0.0f;          // 狙いを控えてからの経過秒
    };

    //! 反動の記録
    struct ReboundRecord
    {
        NS::Core::Vector3 direction{}; // 最後に始めた反動の水平の向き。正規化済み
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
    //! @brief 丸まりを入れるか解き、当たりの形と根の高さを一緒に切り替える
    //! @details 丸まると当たりを球にして根を立ち姿の半長ぶん下げる。
    //! 解くと立ち姿へ戻して、その時の立ち姿の半長ぶん上げる。当たりの下端 (中心 − 半長 − 半径) は動かない。
    //! 根は前フレームの位置と一緒にずらすので、描画の補間に動きとして映らない。今と同じ値なら何もしない
    void ChangeCurled(bool curled) noexcept;
    //! @brief 丸まりを解く
    //! @details 押されていない・突進中でない・直前のフレームを突進中で終えていない・突進の予約が無い・
    //! 接地している・上向きの速度が無い、が揃ったフレームに解く。縁を掴んだ時に解くのは Player::LedgeGrab
    void UncurlWhenSettled() noexcept;
    //! 突進の発動の判定を通れば突進を出す。状態機械を進める前に呼ぶ
    void PrepareStateStep();
    //! 状態機械を進めた後の控えの更新。丸まりを解く判定・長押しの控え・押下の消費・要求と狙いの経過を進める
    void FinishStateStep(float dt);
    //! @brief 掴まり位置から掴める縁を探す
    //! @param[in] hangPos 手を伸ばす元になるカプセル中心の位置
    //! @param[out] outTop 見つけた縁の上端の y。見つからない場合は書き換えない
    //! @return 手の高さ以下の帯に縁があり、登り先も塞がっていない場合 true、それ以外の場合は false
    [[nodiscard]] bool FindLedgeTopAt(const NS::Core::Vector3& hangPos, float& outTop) const noexcept;
    std::unique_ptr<NS::Game::Level::CollisionInput> m_collisionInput;
    bool m_hasInjectedHeld = false; // Update(bool) の間だけ立つ。観測が入力の代わりに m_injectedHeld を使う
    bool m_injectedHeld = false;
    [[nodiscard]] std::string_view ChooseClip(float lateralSpeed) const noexcept;
    [[nodiscard]] float ChoosePlaybackSpeed(std::string_view clip, float lateralSpeed) const noexcept;
    std::unique_ptr<NS::Game::Player::PlayerParams> m_params;
    std::string m_appliedClip{};
    std::unique_ptr<NS::Obj::PlayerInput> m_input;
    std::unique_ptr<NS::Obj::Body> m_body;
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

    float m_maxSpeedScale = 1.0f; // 走行速度に掛ける倍率。書くのは CollisionInput

    // 丸まっているか。入れるのは CollisionInput と突進の発動、解くのは UncurlWhenSettled と縁を掴んだ時と
    // ResetState とプレイを終える時の CollisionInput
    bool m_curled = false;
    bool m_bodySlamHeld = false; // 体当たりのボタンを押しているか。書くのは CollisionInput と ResetState

    BodySlamRecord m_slam;
    BodySlamRequest m_request;
    ReboundRecord m_rebound;

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

//! プレイヤーの配置物か。live の FindPlayer と同じく型名で照合する
[[nodiscard]] bool IsPlayerObject(const nlohmann::json& object) noexcept;

//! シーンの JSON 文書からプレイヤーを探す。最初の 1 件の添字、無ければ k_NoObjectIndex
//! 複数居ても先頭を正とする。2 体以上の警告は EnsurePlayerObject を通した時だけ出る
[[nodiscard]] std::size_t FindPlayerObjectIndex(const nlohmann::json& scene) noexcept;

//! scale は capsule 当たり 0.4/0.9/0.4 に cube mesh の見た目を合わせる値
[[nodiscard]] nlohmann::json MakePlayerObject(const NS::Core::Vector3& position, const NS::Core::Quaternion& rotation);

//! プレイヤーの永続 id。居なければ k_NoObjectId。追従カメラの追従先を結ぶのに使う
[[nodiscard]] std::uint32_t PlayerObjectId(const nlohmann::json& scene) noexcept;

//! プレイヤーが 1 体も居なければ既定構成で足し、永続 id まで振る。2 体以上なら警告して先頭を正とする
//! @retresult 足したなら true
[[nodiscard]] bool EnsurePlayerObject(nlohmann::json& scene);
