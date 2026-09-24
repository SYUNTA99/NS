#include "Game/Player.h"

#include "Game/Level/Finisher.h"
#include "Game/Level/Health.h"
#include "Game/Level/Respawner.h"
#include "Game/Level/ScreenFade.h"
#include "Game/Player/PlayerComponent.h"
#include "Game/Player/PlayerInputRelay.h"
#include "Game/Player/PlayerStateManager.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/Components/Shadow.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <cstring>

NS_CLASS(Player)

Player::Player() noexcept
{
    // 構成と見た目のコード既定。値と追加分はファクトリが player object のデータから写す
    // 同居する部品の引き当ては OnStart なので生成順に縛りは無い
    NS::Obj::MeshRenderer* mesh = AddComponent<NS::Obj::MeshRenderer>();
    // 参照はファクトリが cube mesh と共有 player 材質へ解決する
    mesh->SetMeshRef("cube");
    mesh->SetMaterialRef("player");
    // テクスチャが揃うまでプレイヤーを block と見分ける個体色
    mesh->SetBaseColor(NS::Core::Vector3{0.85f, 0.20f, 0.20f});
    // 2 つで 1 組。状態機械が欠けると遷移が 1 つも起きない
    AddComponent<NS::Game::Player::PlayerStateManager>();
    AddComponent<NS::Game::Player::PlayerComponent>();
    AddComponent<NS::Obj::PlayerInput>();
    // 入力は NS::Obj に居て自機の型を名指しできないので、値の受け渡しを挟む
    AddComponent<NS::Game::Player::PlayerInputRelay>();
    // 命は player 自身の持ち物。ルール配置物がこれを削る
    AddComponent<NS::Game::Level::Health>();
    // 接地シャドウ。mesh / material は後から注入される
    AddComponent<NS::Obj::Shadow>();

    // ルール判定への応答。死んだらやり直す・ゴールでクリアする、はどれもプレイヤーの振る舞いなのでここに積む
    // 暗転はクリアシーケンスが使う部品として隣に置く
    AddComponent<NS::Game::Level::ScreenFade>();
    AddComponent<NS::Game::Level::Respawner>();
    AddComponent<NS::Game::Level::Finisher>();
}

void Player::ApplyDamage(int amount) noexcept
{
    if (NS::Game::Level::Health* health = FindComponent<NS::Game::Level::Health>())
        health->ApplyDamage(amount);
}

void Player::Kill() noexcept
{
    if (NS::Game::Level::Health* health = FindComponent<NS::Game::Level::Health>())
        health->Kill();
}

void Player::ResetHealth() noexcept
{
    if (NS::Game::Level::Health* health = FindComponent<NS::Game::Level::Health>())
        health->Reset();
}

bool Player::IsDead() const noexcept
{
    if (const NS::Game::Level::Health* health = FindComponent<NS::Game::Level::Health>())
        return health->IsDead();
    return false;
}

int Player::Health() const noexcept
{
    if (const NS::Game::Level::Health* health = FindComponent<NS::Game::Level::Health>())
        return health->Current();
    return 0;
}

Player* FindPlayer(NS::Obj::ObjectList& objects) noexcept
{
    for (NS::Obj::GameObject* obj : objects)
    {
        if (std::strcmp(obj->ClassName(), "Player") == 0)
            return static_cast<Player*>(obj);
    }
    return nullptr;
}

bool IsPlayerObject(const nlohmann::json& object) noexcept
{
    return NS::Obj::ObjectJsonClass(object) == "Player";
}

std::size_t FindPlayerObjectIndex(const nlohmann::json& scene) noexcept
{
    const nlohmann::json& objects = NS::Obj::SceneJsonObjects(scene);
    for (std::size_t i = 0; i < objects.size(); ++i)
    {
        if (IsPlayerObject(objects[i]))
            return i;
    }
    return NS::Obj::k_NoObjectIndex;
}

nlohmann::json MakePlayerObject(const NS::Core::Vector3& position, const NS::Core::Quaternion& rotation)
{
    // 構成は Player のコンストラクタが決める。ひな形は型名だけ持ち、値はコード既定を使う
    nlohmann::json object = NS::Obj::MakePrototypeJson<Player>();
    NS::Obj::SetObjectPosition(object, position);
    NS::Obj::SetObjectRotation(object, rotation);
    // cube mesh の半サイズ 0.5 を capsule 当たり radius 0.4 / 半高 0.9 に合わせる倍率
    NS::Obj::SetObjectScale(object, NS::Core::Vector3{0.8f, 1.8f, 0.8f});
    return object;
}

std::uint32_t PlayerObjectId(const nlohmann::json& scene) noexcept
{
    const std::size_t index = FindPlayerObjectIndex(scene);
    if (index == NS::Obj::k_NoObjectIndex)
        return NS::Obj::k_NoObjectId;
    return NS::Obj::ObjectJsonId(NS::Obj::SceneJsonObjects(scene)[index]);
}

bool EnsurePlayerObject(nlohmann::json& scene)
{
    bool created = false;
    if (FindPlayerObjectIndex(scene) == NS::Obj::k_NoObjectIndex)
    {
        // capsule 中心の高さは、床 block 上面 0.5 + capsule 半高 0.9 + 1cm
        NS::Obj::SceneJsonObjects(scene).push_back(
            MakePlayerObject(NS::Core::Vector3{0.0f, 1.41f, 0.0f}, NS::Core::Quaternion{}));
        created = true;
    }

    std::size_t count = 0;
    for (const nlohmann::json& object : NS::Obj::SceneJsonObjects(scene))
    {
        if (IsPlayerObject(object))
            ++count;
    }
    if (count > 1)
        NS_LOG_WARN(Game, "プレイヤーが {} 体ある。先頭の 1 体を正とし、残りは無効として扱う", count);

    // 追従カメラが Target へ書き込む id が要るので、ここで採番まで済ませる
    NS::Obj::EnsureUniqueObjectIds(scene);
    return created;
}
