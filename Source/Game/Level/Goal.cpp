#include "Game/Level/Goal.h"

#include "Game/Level/GoalComponent.h"
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

namespace NS::Game::Level
{
    Goal::Goal() noexcept
    {
        // 目印の金色の立方体
        NS::Obj::MeshRenderer* mesh = AddComponent<NS::Obj::MeshRenderer>();
        mesh->SetMeshRef("cube");
        mesh->SetBaseColor(NS::Core::Vector3{1.0f, 0.84f, 0.0f});
        AddComponent<GoalComponent>();
    }

    bool IsGoalObject(const nlohmann::json& object) noexcept
    {
        return NS::Obj::ObjectJsonClass(object) == "Goal";
    }

    NS_PLACEABLE(Goal, "ゴール")
} // namespace NS::Game::Level
