#pragma once

#include "Runtime/Object/Actor.h"
#include "Runtime/Object/CameraTarget.h"
#include "Runtime/Object/Scene/SceneJson.h"

namespace NS::Obj
{
    class ObjectList;
} // namespace NS::Obj

//! @brief プレイヤーキャラクタ。Mesh / Movement / Input / Health / Shadow の既定構成をコードで組む
//! @details 値はプレイヤーの種類の既定値と個体の上書きから写す
//! 移動やつかみ等の能力 API はここに置き、実装は各 Component が持つ
//! 落下死やゴールは体のセンサーへ届く知らせで受け取り、コースの流れは進行役へ伝えるだけにする
class Player : public NS::Obj::Actor, public NS::Obj::ICameraTarget
{
public:
    //! 既定の構成と見た目で組む。Mesh / Material は後からファクトリが入れる
    Player() noexcept;
    ~Player() override = default;

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    Player(Player&&) = delete;
    Player& operator=(Player&&) = delete;

    //! 保存形式と TypeRegistry の登録名。読込はこの名前で Actor の型を選ぶ
    [[nodiscard]] const char* ClassName() const noexcept override { return "Player"; }

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

    //! 即死。命を 0 にする
    void Kill() noexcept;

    //! 命を満タンへ戻す。プレイ突入とリスタートで呼ばれる
    void ResetHealth() noexcept;

    [[nodiscard]] bool IsDead() const noexcept;
    [[nodiscard]] int Health() const noexcept;
};

//! live の配置物からプレイヤーを引く。無ければ nullptr
//! 型名の照合で見つける。能力は Player の公開関数を呼び、細部が要る側だけ FindComponent で引く
//! @param[in,out] objects 探す先の配置物。返した Player* から中身が書き換わる
[[nodiscard]] Player* FindPlayer(NS::Obj::ObjectList& objects) noexcept;

//! プレイヤーの配置物か。live の FindPlayer と同じく型名で照合する
[[nodiscard]] bool IsPlayerObject(const nlohmann::json& object) noexcept;

//! シーンの JSON 文書からプレイヤーを探す。最初の 1 件の添字、無ければ k_NoObjectIndex
//! 複数居ても先頭を正とする。2 体以上の警告は EnsurePlayerObject を通した時だけ出る
[[nodiscard]] std::size_t FindPlayerObjectIndex(const nlohmann::json& scene) noexcept;

//! 指定の位置と向きでプレイヤーのひな形の JSON を作る。components は型名だけ持ち、値はコード既定を使う
//! scale は capsule 当たり 0.4/0.9/0.4 に cube mesh の見た目を合わせる値
[[nodiscard]] nlohmann::json MakePlayerObject(const NS::Core::Vector3& position, const NS::Core::Quaternion& rotation);

//! プレイヤーの永続 id。居なければ k_NoObjectId。追従カメラの追従先を結ぶのに使う
[[nodiscard]] std::uint32_t PlayerObjectId(const nlohmann::json& scene) noexcept;

//! プレイヤーが 1 体も居なければ既定構成で足し、永続 id まで振る。2 体以上なら警告して先頭を正とする
//! @retresult 足したなら true
[[nodiscard]] bool EnsurePlayerObject(nlohmann::json& scene);
