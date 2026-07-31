#include "Game/Player.h"

#include "Game/Level/AreaCameraActivatorComponent.h"
#include "Game/Level/CoyoteDebugComponent.h"
#include "Game/Level/FinisherComponent.h"
#include "Game/Level/HealthComponent.h"
#include "Game/Level/RespawnerComponent.h"
#include "Game/Level/ScreenFadeComponent.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Object/Components/CharacterMovementComponent.h"
#include "Runtime/Object/Components/MeshRendererComponent.h"
#include "Runtime/Object/Components/PlayerInputComponent.h"
#include "Runtime/Object/Components/ShadowComponent.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/World.h"

#include <cstring>

NS_CLASS(Player)

namespace
{
    // テクスチャが揃うまでプレイヤーを block と見分ける個体色
    constexpr NS::Math::Vector3 k_PlayerBaseColor{0.85f, 0.20f, 0.20f};
} // namespace

Player::Player() noexcept
{
    // 構成と見た目のコード既定。値と追加分はファクトリが player object のデータから写す
    // input は OnStart で兄弟の movement を解決するので生成順に縛りは無い
    auto* mesh = AddComponent<NS::Object::MeshRendererComponent>();
    // 参照はファクトリが cube mesh と共有 player 材質へ解決する
    mesh->SetMeshRef("cube");
    mesh->SetMaterialRef("player");
    mesh->SetBaseColor(k_PlayerBaseColor);
    AddComponent<NS::Object::CharacterMovementComponent>();
    AddComponent<NS::Object::PlayerInputComponent>();
    // 命は player 自身の持ち物。hazard 等のルール配置物がこれを削る
    AddComponent<NS::Game::Level::HealthComponent>();
    // 接地シャドウ。mesh / material / 衝突 world は後から注入される
    AddComponent<NS::Object::ShadowComponent>();

    // ルール判定への応答。死んだらやり直す・ゴールでクリアする・自分の位置を area camera へ配る、は
    // どれもプレイヤーの振る舞いなのでここに積む。暗転はクリア台本が使う道具として隣に置く
    AddComponent<NS::Game::Level::ScreenFadeComponent>();
    AddComponent<NS::Game::Level::RespawnerComponent>();
    AddComponent<NS::Game::Level::FinisherComponent>();
    AddComponent<NS::Game::Level::AreaCameraActivatorComponent>();
#if !defined(NS_SHIPPING)
    AddComponent<NS::Game::Level::CoyoteDebugComponent>();
#endif
}

void Player::ApplyDamage(int amount) noexcept
{
    if (auto* health = FindComponent<NS::Game::Level::HealthComponent>())
        health->ApplyDamage(amount);
}

void Player::Kill() noexcept
{
    if (auto* health = FindComponent<NS::Game::Level::HealthComponent>())
        health->Kill();
}

void Player::ResetHealth() noexcept
{
    if (auto* health = FindComponent<NS::Game::Level::HealthComponent>())
        health->Reset();
}

bool Player::IsDead() const noexcept
{
    if (const auto* health = FindComponent<NS::Game::Level::HealthComponent>())
        return health->IsDead();
    return false;
}

int Player::Health() const noexcept
{
    if (const auto* health = FindComponent<NS::Game::Level::HealthComponent>())
        return health->Current();
    return 0;
}

Player* FindPlayer(const NS::Object::World& world) noexcept
{
    for (NS::Object::GameObject* obj : world)
    {
        if (std::strcmp(obj->ClassName(), "Player") == 0)
            return static_cast<Player*>(obj);
    }
    return nullptr;
}

bool IsPlayerObject(const NS::Object::ObjectData& object) noexcept
{
    return object.className == "Player";
}

std::size_t FindPlayerObjectIndex(const NS::Object::SceneData& level) noexcept
{
    for (std::size_t i = 0; i < level.objects.size(); ++i)
    {
        if (IsPlayerObject(level.objects[i]))
            return i;
    }
    return NS::Object::k_NoObjectIndex;
}

NS::Object::ObjectData MakePlayerObject(const NS::Math::Vector3& position, const NS::Math::Quaternion& rotation)
{
    // 構成は Player のコンストラクタが決める。データは型名だけ持ち、値はコード既定に倒す
    NS::Object::ObjectData object = NS::Object::MakeObjectData<Player>();
    NS::Object::SetObjectPosition(object, position);
    NS::Object::SetObjectRotation(object, rotation);
    // cube mesh の半サイズ 0.5 を capsule 当たり radius 0.4 / 半高 0.9 に合わせる縮小
    NS::Object::SetObjectScale(object, NS::Math::Vector3{0.8f, 1.8f, 0.8f});
    return object;
}

std::uint32_t PlayerObjectId(const NS::Object::SceneData& level) noexcept
{
    const std::size_t index = FindPlayerObjectIndex(level);
    if (index == NS::Object::k_NoObjectIndex)
        return NS::Object::k_NoObjectId;
    return level.objects[index].objectId;
}

bool EnsurePlayerObject(NS::Object::SceneData& level)
{
    bool created = false;
    if (FindPlayerObjectIndex(level) == NS::Object::k_NoObjectIndex)
    {
        level.objects.push_back(
            MakePlayerObject(NS::Math::Vector3{0.0f, Player::k_DefaultSpawnY, 0.0f}, NS::Math::Quaternion{}));
        created = true;
    }

    std::size_t count = 0;
    for (const NS::Object::ObjectData& object : level.objects)
    {
        if (IsPlayerObject(object))
            ++count;
    }
    if (count > 1)
        NS_LOG_WARN(Game, "プレイヤーが {} 体ある。先頭の 1 体を正とし、残りは無効として扱う", count);

    // 追従カメラが Target へ焼く id が要るので、ここで採番まで済ませる
    NS::Object::EnsureUniqueObjectIds(level);
    return created;
}
