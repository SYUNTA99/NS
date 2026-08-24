#include "Game/Level/RespawnerComponent.h"

#include "Game/Level/GoalComponent.h"
#include "Game/Level/HealthComponent.h"
#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/Scene.h"

namespace NS::Game::Level
{
    // 判定の後、カメラ追従の前。同じ LateUpdate でやり直す
    RespawnerComponent::RespawnerComponent() noexcept
        : NS::Object::Component(NS::Object::TickPriority::LateUpdate + 10)
    {}

    void RespawnerComponent::OnUpdate()
    {
        auto* health = Owner()->FindComponent<HealthComponent>();
        if (health == nullptr || !health->IsDead())
            return;

        RestartRun();
    }

    void RespawnerComponent::RestartRun() noexcept
    {
        // プレイヤーに載る component なので、 戻す相手は自分の owner
        auto* scene = static_cast<NS::Object::Scene*>(Owner()->OwningScene());
        if (scene == nullptr)
            return;

        // 出現位置はエディタで配置したプレイヤーの capsule 中心 world 位置そのもの
        // 凍結スナップショットに既にある値なので写しは持たず、その都度読む
        const NS::Object::SceneData& level = scene->PlayBaseline();
        NS::Core::Vector3 spawn{0.0f, ::Player::k_DefaultSpawnY, 0.0f};
        const std::size_t playerIndex = FindPlayerObjectIndex(level);
        if (playerIndex != NS::Object::k_NoObjectIndex)
            spawn = NS::Object::ObjectPosition(level.objects[playerIndex]);

        Owner()->Root().SetPosition(spawn);
        if (auto* movement = Owner()->FindComponent<NS::Game::Player::PlayerComponent>())
            movement->ResetState();
        if (auto* health = Owner()->FindComponent<HealthComponent>())
            health->Reset();

        // ルール配置物のフラグも初期状態へ戻す。前のプレイのフラグが残ると開始直後に再クリアしてしまう
        scene->World().ForEachComponent<GoalComponent>([](GoalComponent& goal) { goal.ResetReached(); });
    }
} // namespace NS::Game::Level
