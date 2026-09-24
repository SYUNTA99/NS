#include "Game/Level/Respawner.h"

#include "Game/Level/Goal.h"
#include "Game/Level/Health.h"
#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/Scene.h"

namespace NS::Game::Level
{
    // 判定の後、カメラ追従の前。同じ LateUpdate でやり直す
    Respawner::Respawner() noexcept
        : NS::Obj::Component(NS::Obj::TickPriority::LateUpdate + 10)
    {}

    void Respawner::OnUpdate()
    {
        auto* health = Owner()->FindComponent<Health>();
        if (health == nullptr || !health->IsDead())
            return;

        RestartRun();
    }

    void Respawner::RestartRun() noexcept
    {
        // プレイヤーに載る component なので、戻す相手は自分の owner
        auto* scene = static_cast<NS::Obj::Scene*>(Owner()->OwningScene());
        if (scene == nullptr)
            return;

        // 出現位置はエディタで配置したプレイヤーの capsule 中心 world 位置そのもの
        // 凍結スナップショットに既にある値なので写しは持たず、その都度読む
        const NS::Obj::SceneData& level = scene->PlayBaseline();
        // プレイヤーが居なければ、新規レベルで置く位置へ戻す
        NS::Core::Vector3 spawn{0.0f, 1.41f, 0.0f};
        const std::size_t playerIndex = FindPlayerObjectIndex(level);
        if (playerIndex != NS::Obj::k_NoObjectIndex)
            spawn = NS::Obj::ObjectPosition(level.objects[playerIndex]);

        Owner()->Root().SetPosition(spawn);
        if (auto* movement = Owner()->FindComponent<NS::Game::Player::PlayerComponent>())
            movement->ResetState();
        if (auto* health = Owner()->FindComponent<Health>())
            health->Reset();

        // ルール配置物のフラグも初期状態へ戻す。前のプレイのフラグが残ると開始直後に再クリアしてしまう
        scene->Objects().ForEachComponent<Goal>([](Goal& goal) { goal.ResetReached(); });
    }
} // namespace NS::Game::Level
