#include "Game/Level/GoalComponent.h"

#include "Game/Player.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"

namespace NS::Game::Level
{
    GoalComponent::GoalComponent() noexcept : NS::Obj::Component(NS::Obj::TickPriority::LateUpdate) {}

    void GoalComponent::OnUpdate()
    {
        if (m_reached)
        {
            return;
        }

        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        ::Player* player = FindPlayer(scene->Objects());
        if (player == nullptr)
        {
            return;
        }

        // 中心間距離の単純比較。触れた事実はフラグとして保持し、離れても下ろさない
        const NS::Core::Vector3 toPlayer = player->Root().Position() - RootTransform().Position();
        if (toPlayer.LengthSquared() < m_radius * m_radius)
        {
            m_reached = true;
        }
    }

    NS_CLASS(GoalComponent)
} // namespace NS::Game::Level
