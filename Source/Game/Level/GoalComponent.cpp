#include "Game/Level/GoalComponent.h"

#include "Game/Player.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/World.h"

namespace NS::Game::Level
{
    GoalComponent::GoalComponent() noexcept
        : NS::Object::Component(NS::Object::TickPriority::LateUpdate)
    {}

    void GoalComponent::OnUpdate()
    {
        if (m_reached)
            return;

        auto* scene = Owner()->OwningScene();
        if (scene == nullptr)
            return;
        auto* player = FindPlayer(scene->World());
        if (player == nullptr)
            return;

        // 中心間距離の単純比較。触れた事実はフラグとして保持し、離れても下ろさない
        const NS::Math::Vector3 toPlayer = player->Root().Position() - RootTransform().Position();
        if (toPlayer.LengthSquared() < k_GoalRadius * k_GoalRadius)
            m_reached = true;
    }

    bool IsGoalObject(const NS::Object::ObjectData& object) noexcept
    {
        return NS::Object::FindComponentEntry(object, "GoalComponent") != nullptr;
    }

    NS_CLASS(GoalComponent)
} // namespace NS::Game::Level
