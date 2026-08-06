#include "Game/Level/BlockObject.h"

#include "Game/Level/GoalComponent.h"
#include "Game/Level/HazardComponent.h"
#include "Game/Level/KillZoneComponent.h"
#include "Runtime/Object/Components/BoxColliderComponent.h"
#include "Runtime/Object/Components/SlopeColliderComponent.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"

namespace NS::Game::Level
{
    nlohmann::json MakeMeshRendererEntry(std::string_view meshName,
                                         std::string_view materialName,
                                         const NS::Math::Vector3& baseColor)
    {
        nlohmann::json entry = NS::Object::MakeComponentEntry("MeshRendererComponent");
        NS::Object::SetField(entry, "メッシュ", meshName);
        NS::Object::SetField(entry, "マテリアル", materialName);
        NS::Object::SetField(entry, "基本色", baseColor);
        return entry;
    }

    nlohmann::json MakeCellCubeComponents()
    {
        nlohmann::json box = NS::Object::MakeComponentEntry("BoxColliderComponent");
        NS::Object::SetField(box, "半径", k_CellHalfExtents);
        return nlohmann::json::array({MakeMeshRendererEntry("cube", "", k_SolidBaseColor), std::move(box)});
    }

    NS::Object::ObjectData MakeCellObject(std::int16_t x, std::int16_t y, std::int16_t z)
    {
        NS::Object::ObjectData object{};
        object.components = MakeCellCubeComponents();
        // components を確定した後に transform を書き込む。 先に書くと components 代入が TransformComponent を消す
        NS::Object::SetObjectPosition(
            object, NS::Math::Vector3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
        return object;
    }

    bool IsSolidBoxRule(bool hasBox, bool hasSlope, bool hasHazard, bool hasGoal, bool hasKillZone) noexcept
    {
        return hasBox && !hasSlope && !hasHazard && !hasGoal && !hasKillZone;
    }

    std::optional<NS::Math::OBB> SolidBoxWorldOBB(NS::Object::GameObject& obj) noexcept
    {
        auto* box = obj.FindComponent<NS::Object::BoxColliderComponent>();
        const bool hasSlope = obj.FindComponent<NS::Object::SlopeColliderComponent>() != nullptr;
        const bool hasHazard = obj.FindComponent<HazardComponent>() != nullptr;
        const bool hasGoal = obj.FindComponent<GoalComponent>() != nullptr;
        const bool hasKillZone = obj.FindComponent<KillZoneComponent>() != nullptr;

        if (!IsSolidBoxRule(box != nullptr, hasSlope, hasHazard, hasGoal, hasKillZone))
        {
            return std::nullopt;
        }
        return box->WorldOBB();
    }
} // namespace NS::Game::Level
