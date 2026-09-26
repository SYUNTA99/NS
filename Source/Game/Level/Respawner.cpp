#include "Game/Level/Respawner.h"

#include "Game/Level/Goal.h"
#include "Game/Level/Health.h"
#include "Game/Level/LaunchedBody.h"
#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/ObjectJson.h"
#include "Runtime/Object/Scene/Scene.h"

namespace NS::Game::Level
{
    // 判定の後、カメラ追従の前。同じ LateUpdate でやり直す
    Respawner::Respawner() noexcept : NS::Obj::Component(NS::Obj::TickPriority::LateUpdate + 10) {}

    void Respawner::OnUpdate()
    {
        Health* health = Owner()->FindComponent<Health>();
        if (health == nullptr || !health->IsDead())
            return;

        RestartRun();
    }

    void Respawner::RestartRun() noexcept
    {
        // プレイヤーに載る component なので、戻す相手は自分の owner
        NS::Obj::Scene* scene = static_cast<NS::Obj::Scene*>(Owner()->OwningScene());
        if (scene == nullptr)
            return;

        // 出現位置はエディタで配置したプレイヤーの capsule 中心 world 位置そのもの
        // 凍結スナップショットに既にある値なので写しは持たず、その都度読む
        const nlohmann::json& level = scene->PlayBaseline();
        // プレイヤーが居なければ、新規レベルで置く位置へ戻す
        NS::Core::Vector3 spawn{0.0f, 1.41f, 0.0f};
        const std::size_t playerIndex = FindPlayerObjectIndex(level);
        if (playerIndex != NS::Obj::k_NoObjectIndex)
            spawn = NS::Obj::ObjectPosition(NS::Obj::SceneJsonObjects(level)[playerIndex]);

        Owner()->Root().SetPosition(spawn);
        if (NS::Game::Player::PlayerComponent* movement = Owner()->FindComponent<NS::Game::Player::PlayerComponent>())
            movement->ResetState();
        if (Health* health = Owner()->FindComponent<Health>())
            health->Reset();

        // 飛ばした物も置いた所へ戻し、コースを同じ形でやり直させる
        // 凍結に無い物 (破片・実行時に足した物) は番号で引けないので触らない
        // 作り直さずに値を戻す。更新の最中に配置物を消すと、集めた更新の並びに解放済みの位置が残る
        for (const nlohmann::json& placed : NS::Obj::SceneJsonObjects(level))
        {
            NS::Obj::GameObject* live = scene->Objects().FindByObjectId(NS::Obj::ObjectJsonId(placed));
            if (live == nullptr)
            {
                continue;
            }
            LaunchedBody* body = live->FindComponent<LaunchedBody>();
            if (body == nullptr)
            {
                continue;
            }
            body->ResetTo(NS::Obj::ObjectPosition(placed), NS::Obj::ObjectRotation(placed));
        }

        // ルール配置物のフラグも初期状態へ戻す。前のプレイのフラグが残ると開始直後に再クリアしてしまう
        scene->Objects().ForEachComponent<Goal>([](Goal& goal) { goal.ResetReached(); });
    }
} // namespace NS::Game::Level
