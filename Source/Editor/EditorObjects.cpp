#include "Editor/EditorObjects.h"

#include "Game/Level/BlockObject.h"
#include "Game/Level/FollowCameraObject.h"
#include "Game/Level/GoalComponent.h"
#include "Game/Level/HazardComponent.h"
#include "Game/Level/KillZoneComponent.h"
#include "Game/Player.h"
#include "Runtime/Object/Components/BoxColliderComponent.h"
#include "Runtime/Object/Components/DirectionalLightComponent.h"
#include "Runtime/Object/Components/MeshRendererComponent.h"
#include "Runtime/Object/Components/PlacedVirtualCamera.h"
#include "Runtime/Object/Components/PlayerInputComponent.h"
#include "Runtime/Object/Components/SlopeColliderComponent.h"
#include "Runtime/Object/Components/SphereColliderComponent.h"
#include "Runtime/Object/Components/ThirdPersonFollowComponent.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/World.h"

namespace NS::Editor
{
    namespace
    {
        // cell ブラシの回転値 0..3 を Y 軸 90° 刻みの yaw ラジアンへ写す係数
        constexpr float k_QuarterTurnYaw = NS::Math::k_Pi * 0.5f;

        bool HasComponentType(const NS::Object::ObjectData& object, const char* typeName) noexcept
        {
            return NS::Object::FindComponentEntry(object, typeName) != nullptr;
        }

        // Transform しか持たない = 中身が無い GameObject。 データ側は Transform も component 列に居る
        bool HasOnlyTransform(const NS::Object::ObjectData& object) noexcept
        {
            for (const nlohmann::json& entry : object.components)
            {
                if (NS::Object::ComponentEntryType(entry) != "TransformComponent")
                    return false;
            }
            return true;
        }

        float SlopeAngleOf(const NS::Object::ObjectData& object) noexcept
        {
            const nlohmann::json* slope = NS::Object::FindComponentEntry(object, "SlopeColliderComponent");
            if (!slope)
                return -1.0f;
            return NS::Object::FieldFloat(*slope, "Angle (deg)", 0.0f);
        }

        std::string MaterialRefOf(const NS::Object::ObjectData& object)
        {
            const nlohmann::json* renderer = NS::Object::FindComponentEntry(object, "MeshRendererComponent");
            if (!renderer)
                return {};
            return NS::Object::FieldString(*renderer, "Material", {});
        }
    } // namespace

    std::int16_t ObjectCellX(const NS::Object::ObjectData& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(NS::Object::ObjectPosition(object).x));
    }

    std::int16_t ObjectCellY(const NS::Object::ObjectData& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(NS::Object::ObjectPosition(object).y));
    }

    std::int16_t ObjectCellZ(const NS::Object::ObjectData& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(NS::Object::ObjectPosition(object).z));
    }

    bool IsCellBrushObject(const NS::Object::ObjectData& object) noexcept
    {
        // プレイヤーとカメラは別経路で扱うため cell ブラシの対象から外す
        return !IsPlayerObject(object) && !NS::Game::Level::IsFollowCameraObject(object) &&
               NS::Object::FindComponentEntry(object, "PlacedVirtualCamera") == nullptr;
    }

    bool IsCellBrushObject(NS::Object::GameObject& object) noexcept
    {
        // 実行時の一時オブジェクトは配置物でないため対象外
        return !object.IsTransient() && object.FindComponent<NS::Object::PlayerInputComponent>() == nullptr &&
               object.FindComponent<NS::Object::ThirdPersonFollowComponent>() == nullptr &&
               object.FindComponent<NS::Object::PlacedVirtualCamera>() == nullptr;
    }

    std::int16_t ObjectCellX(const NS::Object::GameObject& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(object.Root().Position().x));
    }

    std::int16_t ObjectCellY(const NS::Object::GameObject& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(object.Root().Position().y));
    }

    std::int16_t ObjectCellZ(const NS::Object::GameObject& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(object.Root().Position().z));
    }

    std::size_t FindObjectAtCell(const NS::Object::SceneData& level,
                                 std::int16_t x,
                                 std::int16_t y,
                                 std::int16_t z) noexcept
    {
        for (std::size_t i = 0; i < level.objects.size(); ++i)
        {
            const auto& object = level.objects[i];
            if (IsCellBrushObject(object) && ObjectCellX(object) == x && ObjectCellY(object) == y &&
                ObjectCellZ(object) == z)
            {
                return i;
            }
        }
        return NS::Object::k_NoObjectIndex;
    }

    std::uint32_t FindObjectIdAtCell(const NS::Object::World& world,
                                     std::int16_t x,
                                     std::int16_t y,
                                     std::int16_t z) noexcept
    {
        for (NS::Object::GameObject* objPtr : world)
        {
            NS::Object::GameObject& object = *objPtr;
            if (IsCellBrushObject(object) && ObjectCellX(object) == x && ObjectCellY(object) == y &&
                ObjectCellZ(object) == z)
            {
                return object.Id();
            }
        }
        return NS::Object::k_NoObjectId;
    }

    std::uint8_t CellRotationStep(const NS::Object::ObjectData& object) noexcept
    {
        // q と -q は同じ回転なので fabs で符号を無視し、4 候補から一番近いものを選ぶ
        const NS::Math::Quaternion current = NS::Object::ObjectRotation(object);
        std::uint8_t best = 0;
        float bestDot = -2.0f;
        for (std::uint8_t step = 0; step < 4; ++step)
        {
            const float yaw = static_cast<float>(step) * k_QuarterTurnYaw;
            const NS::Math::Quaternion candidate = NS::Math::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
            const float dot = std::fabs(current.x * candidate.x + current.y * candidate.y + current.z * candidate.z +
                                        current.w * candidate.w);
            if (dot > bestDot)
            {
                bestDot = dot;
                best = step;
            }
        }
        return best;
    }

    void SetCellRotationStep(NS::Object::ObjectData& object, std::uint8_t rotationStep) noexcept
    {
        const float yaw = static_cast<float>(rotationStep & 0x03) * k_QuarterTurnYaw;
        const NS::Math::Quaternion rotation = NS::Math::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
        NS::Object::SetObjectRotation(object, rotation);
    }

    nlohmann::json MakeCellSlopeComponents(float angleDegrees)
    {
        const char* meshName = "wedge45";
        if (angleDegrees < 18.75f)
            meshName = "wedge15";
        else if (angleDegrees < 26.25f)
            meshName = "wedge22";
        else if (angleDegrees < 37.5f)
            meshName = "wedge30";

        nlohmann::json slope = NS::Object::MakeComponentEntry("SlopeColliderComponent");
        NS::Object::SetField(slope, "Angle (deg)", angleDegrees);
        NS::Object::SetField(slope, "Half Extents", NS::Game::Level::k_CellHalfExtents);
        return nlohmann::json::array(
            {NS::Game::Level::MakeMeshRendererEntry(meshName, "", NS::Game::Level::k_SolidBaseColor),
             std::move(slope)});
    }

    nlohmann::json MakePrimitiveComponents(PrimitiveKind kind)
    {
        switch (kind)
        {
        case PrimitiveKind::Cube:
            return NS::Game::Level::MakeCellCubeComponents();
        case PrimitiveKind::Sphere:
        {
            nlohmann::json sphere = NS::Object::MakeComponentEntry("SphereColliderComponent");
            NS::Object::SetField(sphere, "Radius", NS::Game::Level::k_CellHalfExtents.y);
            return nlohmann::json::array(
                {NS::Game::Level::MakeMeshRendererEntry("sphere", "", NS::Game::Level::k_SolidBaseColor),
                 std::move(sphere)});
        }
        case PrimitiveKind::Slope:
            return MakeCellSlopeComponents(45.0f);
        case PrimitiveKind::Empty:
            break;
        }
        return nlohmann::json::array();
    }

    nlohmann::json MakeGoalComponents()
    {
        // 視覚用マーカーとして金色のキューブを付与する
        return nlohmann::json::array(
            {NS::Game::Level::MakeMeshRendererEntry("cube", "", NS::Math::Vector3{1.0f, 0.84f, 0.0f}),
             NS::Object::MakeComponentEntry("GoalComponent")});
    }

    bool IsSolidObject(const NS::Object::ObjectData& object)
    {
        const bool hasBox = HasComponentType(object, "BoxColliderComponent");
        const bool hasSlope = HasComponentType(object, "SlopeColliderComponent");
        const bool hasHazard = HasComponentType(object, "HazardComponent");
        const bool hasGoal = NS::Game::Level::IsGoalObject(object);
        const bool hasKillZone = NS::Game::Level::IsKillZoneObject(object);
        return NS::Game::Level::IsSolidBoxRule(hasBox, hasSlope, hasHazard, hasGoal, hasKillZone);
    }

    bool IsRotatableObject(const NS::Object::ObjectData& object)
    {
        return SlopeAngleOf(object) >= 0.0f || IsSolidObject(object);
    }

    const char* ObjectDisplayName(const NS::Object::ObjectData& object)
    {
        if (!object.name.empty())
            return object.name.c_str();
        if (IsPlayerObject(object))
            return "Player";
        if (HasComponentType(object, "ThirdPersonFollowComponent"))
            return "Follow Camera";
        if (HasComponentType(object, "PlacedVirtualCamera"))
            return "Camera";
        if (HasComponentType(object, "DirectionalLightComponent"))
            return "Directional Light";

        if (NS::Game::Level::IsGoalObject(object))
            return "Goal";

        const float slopeAngle = SlopeAngleOf(object);
        if (slopeAngle >= 0.0f)
        {
            if (slopeAngle >= 44.0f)
                return "Slope 45";
            if (slopeAngle >= 29.0f)
                return "Slope 30";
            if (slopeAngle >= 22.0f)
                return "Slope 22.5";
            if (slopeAngle >= 14.0f)
                return "Slope 15";
            return "Slope";
        }

        if (HasComponentType(object, "HazardComponent"))
            return "Hazard";
        if (MaterialRefOf(object) == "water")
            return "Water";
        if (HasComponentType(object, "SphereColliderComponent"))
            return "Sphere";
        if (HasComponentType(object, "BoxColliderComponent"))
            return "Solid";
        if (HasComponentType(object, "MeshRendererComponent"))
            return "Decoration";
        if (HasOnlyTransform(object))
            return "Empty";

        return "?";
    }

    const char* ObjectDisplayName(NS::Object::GameObject& object)
    {
        if (!object.Name().empty())
            return object.Name().c_str();
        if (object.FindComponent<NS::Object::PlayerInputComponent>() != nullptr)
            return "Player";
        if (object.FindComponent<NS::Object::ThirdPersonFollowComponent>() != nullptr)
            return "Follow Camera";
        if (object.FindComponent<NS::Object::PlacedVirtualCamera>() != nullptr)
            return "Camera";
        if (object.FindComponent<NS::Object::DirectionalLightComponent>() != nullptr)
            return "Directional Light";

        if (object.FindComponent<NS::Game::Level::GoalComponent>() != nullptr)
            return "Goal";

        if (auto* slope = object.FindComponent<NS::Object::SlopeColliderComponent>())
        {
            const float slopeAngle = slope->AngleDegrees();
            if (slopeAngle >= 44.0f)
                return "Slope 45";
            if (slopeAngle >= 29.0f)
                return "Slope 30";
            if (slopeAngle >= 22.0f)
                return "Slope 22.5";
            if (slopeAngle >= 14.0f)
                return "Slope 15";
            return "Slope";
        }

        if (object.FindComponent<NS::Game::Level::HazardComponent>() != nullptr)
            return "Hazard";
        auto* mesh = object.FindComponent<NS::Object::MeshRendererComponent>();
        if (mesh != nullptr && mesh->MaterialRef() == "water")
            return "Water";
        if (object.FindComponent<NS::Object::SphereColliderComponent>() != nullptr)
            return "Sphere";
        if (object.FindComponent<NS::Object::BoxColliderComponent>() != nullptr)
            return "Solid";
        if (mesh != nullptr)
            return "Decoration";
        // GameObject が必ず持つ TransformComponent 1 つだけなら中身が無い
        if (object.Components().size() == 1)
            return "Empty";

        return "?";
    }

} // namespace NS::Editor
