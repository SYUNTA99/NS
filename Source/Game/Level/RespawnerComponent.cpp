#include "Game/Level/RespawnerComponent.h"

#include "Game/Level/GoalComponent.h"
#include "Game/Level/HealthComponent.h"
#include "Game/Player.h"
#include "Runtime/Object/Components/CharacterMovementComponent.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/Scene.h"

namespace NS::Game::Level
{
    // ルール判定 (LateUpdate) が出そろった後、カメラ追従 (+50) の前に同じ tick で応える
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
        // プレイヤーに載る component なので、 戻す相手は自分の持ち主
        auto* scene = static_cast<NS::Object::Scene*>(Owner()->OwningScene());
        if (scene == nullptr)
            return;

        // 出現位置はエディタで配置したプレイヤーの capsule 中心 world 位置そのもの
        // 凍結スナップショットに既にある値なので写しは持たず、その都度読む
        const NS::Object::SceneData& level = scene->PlayBaseline();
        NS::Math::Vector3 spawn{0.0f, Player::k_DefaultSpawnY, 0.0f};
        const std::size_t playerIndex = FindPlayerObjectIndex(level);
        if (playerIndex != NS::Object::k_NoObjectIndex)
            spawn = NS::Object::ObjectPosition(level.objects[playerIndex]);

        Owner()->Root().SetPosition(spawn);
        if (auto* movement = Owner()->FindComponent<NS::Object::CharacterMovementComponent>())
            movement->ResetState();
        if (auto* health = Owner()->FindComponent<HealthComponent>())
            health->Reset();

        // ルール配置物の旗も頭から。前のプレイの旗が残ると開始直後に再クリアしてしまう
        scene->World().ForEachComponent<GoalComponent>([](GoalComponent& goal) { goal.ResetReached(); });
    }
} // namespace NS::Game::Level
