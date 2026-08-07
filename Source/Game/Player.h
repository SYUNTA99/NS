#pragma once

#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/SceneData.h"

namespace NS::Object
{
    class World;
} // namespace NS::Object

/// @brief プレイヤーキャラクタ。Mesh / Movement / Input / Health / Shadow の既定構成をコードで組む
/// @details 値と追加の component はファクトリが player object のデータから写す
/// 移動やつかみ等の能力 API はここに置き、実装は各 Component が持つ
/// hazard / KillZone 等のルール配置物は FindPlayer で得た Player* へ能力を呼ぶ。プレイヤーはルールを知らない
class Player : public NS::Object::GameObject
{
public:
    /// 新規レベルでプレイヤーを置く capsule 中心の高さ。床 block 上面 0.5 + capsule 半高 0.9 + 1cm
    static constexpr float k_DefaultSpawnY = 1.41f;

    /// 既定の構成と見た目で組む。Mesh / Material は後からファクトリが入れる
    Player() noexcept;
    ~Player() override = default;

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    Player(Player&&) = delete;
    Player& operator=(Player&&) = delete;

    /// 保存形式と TypeRegistry の登録名。 読込はこの名前で GameObject の型を選ぶ
    [[nodiscard]] const char* ClassName() const noexcept override { return "Player"; }

    /// 命を amount 削る。下限 0
    void ApplyDamage(int amount) noexcept;

    /// 即死。命を 0 にする
    void Kill() noexcept;

    /// 命を満タンへ戻す。プレイ突入とリスタートで呼ばれる
    void ResetHealth() noexcept;

    [[nodiscard]] bool IsDead() const noexcept;
    [[nodiscard]] int Health() const noexcept;
};

/// live world からプレイヤーを引く。無ければ nullptr
/// 型名の照合で見つける。能力は Player の公開関数を呼び、細部が要る側だけ FindComponent で引く
[[nodiscard]] Player* FindPlayer(const NS::Object::World& world) noexcept;

/// プレイヤーの配置物か。live の FindPlayer と同じく型名で照合する
[[nodiscard]] bool IsPlayerObject(const NS::Object::ObjectData& object) noexcept;

/// objects からプレイヤーを探す。最初の 1 件の添字、無ければ k_NoObjectIndex
/// 複数居ても先頭を正とする。余分は読込時に警告済み
[[nodiscard]] std::size_t FindPlayerObjectIndex(const NS::Object::SceneData& level) noexcept;

/// 指定の位置と向きでプレイヤーの ObjectData を作る。components は型名だけ持ち、値はコード既定を使う
/// scale は capsule 当たり 0.4/0.9/0.4 に cube mesh の見た目を合わせる値
[[nodiscard]] NS::Object::ObjectData MakePlayerObject(const NS::Core::Vector3& position,
                                                       const NS::Core::Quaternion& rotation);

/// プレイヤーの永続 id。居なければ k_NoObjectId。追従カメラの追従先を結ぶのに使う
[[nodiscard]] std::uint32_t PlayerObjectId(const NS::Object::SceneData& level) noexcept;

/// プレイヤーが 1 体も居なければ既定構成で足し、永続 id まで振る。2 体以上なら警告して先頭を正とする
/// @retresult 足したなら true
[[nodiscard]] bool EnsurePlayerObject(NS::Object::SceneData& level);
