#pragma once

#include "Game/Level/Health.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/CameraTarget.h"
#include "Runtime/Object/Scene/SceneJson.h"
#include "Runtime/Object/StateMachine.h"

#include <string>
#include <string_view>

namespace NS::Obj
{
    class ObjectList;
    class PlayerInput;
} // namespace NS::Obj

namespace NS::Game::Player
{
    class PlayerComponent;
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

//! @brief プレイヤーキャラクタ。Mesh / Movement / Input / Shadow の既定構成をコードで組む
//! @details 値はプレイヤーの種類の既定値と個体の上書きから写す
//! 状態機械と命は Actor 自身が持ち、移動やつかみ等の実装は移行中の Component が持つ
//! 落下死やゴールは体のセンサーへ届く知らせで受け取り、コースの流れは進行役へ伝えるだけにする
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
    [[nodiscard]] NS::Game::Player::PlayerComponent& Movement() noexcept { return *m_movement; }
    [[nodiscard]] const NS::Game::Player::PlayerComponent& Movement() const noexcept { return *m_movement; }
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
    void Update() override;
    //! @brief 自機の部品を決めた順に 1 固定ステップ進める
    //! @details Model の控え、溜めの判定と衝突の裁定、状態機械の 1 歩、突進の向きの寄せ、PlayerComponent、
    //! クリップの選択、TargetMarker、SlamArrow、HitReaction、PlayerAppearance、ChargeEffects、ImpactEffects の順に呼ぶ
    //! @param[in] chargeHeld 体当たりのボタンを押しているか
    void Update(bool chargeHeld);
    //! @brief 状態と水平の速さから再生するクリップと速度を選び、同居する Animation へ渡す
    //! @details Animation が無ければ何もしない。選んだクリップが無ければ立ちのクリップへ戻す
    void UpdateAnimation();
    void OnEndPlay() override;

    // 移動の組み立て。状態が呼ぶ順序がそのまま手触りになる。速度・接地・重力の計算は PlayerComponent (身体) が持ち、
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
    //! 上昇と下降で非対称な重力を当てる。頂点の近くは弱める
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

    //! @brief 即死する。命を 0 にし、溜めを終えて世界から消える
    //! @details 配置を止めるだけの Kill / SetActive(false) は命に触らない。死は落下死などの知らせだけが起こす
    void Die() noexcept;

    //! @brief 命を満タンへ戻す。プレイ突入とリスタートで呼ばれる
    //! @details 満タンは呼ぶたびに調整値の「体力」から読む。エディタで書き換えた値が次の再生から効く
    void ResetHealth() noexcept;

    [[nodiscard]] bool IsDead() const noexcept;
    [[nodiscard]] int Health() const noexcept;

private:
    friend class NS::Game::Level::CollisionInput;
    class ChargeState;
    void StepCharge(bool held, float dt);
    NS::Obj::SubStateMachine<Player> m_charge;
    std::unique_ptr<NS::Game::Level::CollisionInput> m_collisionInput;
    bool m_chargeHeld = false;
    float m_chargeDelta = 0.0f;
    [[nodiscard]] std::string_view ChooseClip(float lateralSpeed) const noexcept;
    [[nodiscard]] float ChoosePlaybackSpeed(std::string_view clip, float lateralSpeed) const noexcept;
    std::unique_ptr<NS::Game::Player::PlayerParams> m_params;
    std::string m_appliedClip{};
    std::unique_ptr<NS::Obj::PlayerInput> m_input;
    std::unique_ptr<NS::Game::Player::PlayerComponent> m_movement;
    std::unique_ptr<NS::Game::Player::PlayerAppearance> m_appearance;
    std::unique_ptr<NS::Game::Level::ImpactResolver> m_resolver;
    std::unique_ptr<NS::Game::Level::TargetMarker> m_targetMarker;
    std::unique_ptr<NS::Game::Level::SlamArrow> m_slamArrow;
    std::unique_ptr<NS::Game::Player::ChargeEffects> m_chargeEffects;
    std::unique_ptr<NS::Game::Player::ImpactEffects> m_impactEffects;
    NS::Obj::StateMachine<Player>* m_states = nullptr; // 基底が所有する。コンストラクタが預けた直後から有効
    NS::Game::Level::Health m_health;
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
