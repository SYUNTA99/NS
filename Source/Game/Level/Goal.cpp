#include "Game/Level/Goal.h"

#include "Game/Player.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"

namespace NS::Game::Level
{
    Goal::Goal() noexcept : NS::Obj::Component(NS::Obj::TickPriority::LateUpdate) {}

    void Goal::OnUpdate()
    {
        if (m_reached)
        {
            return;
        }

        auto* scene = Owner()->OwningScene();
        if (scene == nullptr)
        {
			return;
        }
        auto* player = FindPlayer(scene->Objects());
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

    bool IsGoalObject(const NS::Obj::ObjectData& object) noexcept
    {
        return NS::Obj::FindComponentEntry(object, "Goal") != nullptr;
    }

    NS_CLASS(Goal)
} // namespace NS::Game::Level
