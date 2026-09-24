#include "Editor/EditorObjects.h"

#include "Game/Level/BlockObject.h"
#include "Game/Level/FollowCameraObject.h"
#include "Game/Level/Goal.h"
#include "Game/Level/Hazard.h"
#include "Game/Level/KillZone.h"
#include "Game/Player.h"
#include "Runtime/Graphics/Mesh.h"
#include "Runtime/Object/Components/BoxCollider.h"
#include "Runtime/Object/Components/DirectionalLight.h"
#include "Runtime/Object/Components/PhysicsSettings.h"
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/Components/PlacedVirtualCamera.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/Components/SlopeCollider.h"
#include "Runtime/Object/Components/SphereCollider.h"
#include "Runtime/Object/Components/ThirdPersonFollow.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"

namespace NS::Editor
{
    namespace
    {
        // cell ブラシの回転値 0..3 を Y 軸 90° 刻みの yaw ラジアンへ写す係数
        constexpr float k_QuarterTurnYaw = NS::Core::k_Pi * 0.5f;

        bool HasComponentType(const NS::Obj::ObjectData& object, const char* typeName) noexcept
        {
            return NS::Obj::FindComponentEntry(object, typeName) != nullptr;
        }

        // Transform しか持たない = 中身が無い GameObject。データ側は Transform も component 列に居る
        bool HasOnlyTransform(const NS::Obj::ObjectData& object) noexcept
        {
            for (const nlohmann::json& entry : object.components)
            {
                if (NS::Obj::ComponentEntryType(entry) != "TransformComponent")
                    return false;
            }
            return true;
        }

        float SlopeAngleOf(const NS::Obj::ObjectData& object) noexcept
        {
            const nlohmann::json* slope = NS::Obj::FindComponentEntry(object, "SlopeCollider");
            if (!slope)
                return -1.0f;
            return NS::Obj::FieldFloat(*slope, "角度 (度)", 0.0f);
        }

        std::string MaterialRefOf(const NS::Obj::ObjectData& object)
        {
            const nlohmann::json* renderer = NS::Obj::FindComponentEntry(object, "MeshRenderer");
            if (!renderer)
                return {};
            return NS::Obj::FieldString(*renderer, "マテリアル", {});
        }
    } // namespace

    std::int16_t ObjectCellX(const NS::Obj::ObjectData& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(NS::Obj::ObjectPosition(object).x));
    }

    std::int16_t ObjectCellY(const NS::Obj::ObjectData& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(NS::Obj::ObjectPosition(object).y));
    }

    std::int16_t ObjectCellZ(const NS::Obj::ObjectData& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(NS::Obj::ObjectPosition(object).z));
    }

    bool IsCellBrushObject(const NS::Obj::ObjectData& object) noexcept
    {
        // プレイヤーとカメラは別経路で扱うため cell ブラシの対象から外す
        return !IsPlayerObject(object) && !NS::Game::Level::IsFollowCameraObject(object) &&
               NS::Obj::FindComponentEntry(object, "PlacedVirtualCamera") == nullptr;
    }

    bool IsCellBrushObject(NS::Obj::GameObject& object) noexcept
    {
        // 実行時の一時オブジェクトは配置物でないため対象外
        return !object.IsTransient() && object.FindComponent<NS::Obj::PlayerInput>() == nullptr &&
               object.FindComponent<NS::Obj::ThirdPersonFollow>() == nullptr &&
               object.FindComponent<NS::Obj::PlacedVirtualCamera>() == nullptr;
    }

    std::int16_t ObjectCellX(const NS::Obj::GameObject& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(object.Root().Position().x));
    }

    std::int16_t ObjectCellY(const NS::Obj::GameObject& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(object.Root().Position().y));
    }

    std::int16_t ObjectCellZ(const NS::Obj::GameObject& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(object.Root().Position().z));
    }

    std::size_t FindObjectAtCell(const NS::Obj::SceneData& level,
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
        return NS::Obj::k_NoObjectIndex;
    }

    std::uint32_t FindObjectIdAtCell(const NS::Obj::ObjectList& objects,
                                     std::int16_t x,
                                     std::int16_t y,
                                     std::int16_t z) noexcept
    {
        for (NS::Obj::GameObject* objPtr : objects)
        {
            NS::Obj::GameObject& object = *objPtr;
            if (IsCellBrushObject(object) && ObjectCellX(object) == x && ObjectCellY(object) == y &&
                ObjectCellZ(object) == z)
            {
                return object.Id();
            }
        }
        return NS::Obj::k_NoObjectId;
    }

    std::uint8_t CellRotationStep(const NS::Obj::ObjectData& object) noexcept
    {
        // q と -q は同じ回転なので fabs で符号を無視し、4 候補から一番近いものを選ぶ
        const NS::Core::Quaternion current = NS::Obj::ObjectRotation(object);
        std::uint8_t best = 0;
        float bestDot = -2.0f;
        for (std::uint8_t step = 0; step < 4; ++step)
        {
            const float yaw = static_cast<float>(step) * k_QuarterTurnYaw;
            const NS::Core::Quaternion candidate = NS::Core::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
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

    void SetCellRotationStep(NS::Obj::ObjectData& object, std::uint8_t rotationStep) noexcept
    {
        const float yaw = static_cast<float>(rotationStep & 0x03) * k_QuarterTurnYaw;
        const NS::Core::Quaternion rotation = NS::Core::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
        NS::Obj::SetObjectRotation(object, rotation);
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

        nlohmann::json slope = NS::Obj::MakeComponentEntry("SlopeCollider");
        NS::Obj::SetField(slope, "角度 (度)", angleDegrees);
        NS::Obj::SetField(slope, "半径", NS::Game::Level::k_CellHalfExtents);
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
            nlohmann::json sphere = NS::Obj::MakeComponentEntry("SphereCollider");
            NS::Obj::SetField(sphere, "半径", NS::Game::Level::k_CellHalfExtents.y);
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
            {NS::Game::Level::MakeMeshRendererEntry("cube", "", NS::Core::Vector3{1.0f, 0.84f, 0.0f}),
             NS::Obj::MakeComponentEntry("Goal")});
    }

    bool IsSolidObject(const NS::Obj::ObjectData& object)
    {
        const bool hasBox = HasComponentType(object, "BoxCollider");
        const bool hasSlope = HasComponentType(object, "SlopeCollider");
        const bool hasHazard = HasComponentType(object, "Hazard");
        const bool hasGoal = NS::Game::Level::IsGoalObject(object);
        const bool hasKillZone = NS::Game::Level::IsKillZoneObject(object);
        return hasBox && !hasSlope && !hasHazard && !hasGoal && !hasKillZone;
    }

    bool IsRotatableObject(const NS::Obj::ObjectData& object)
    {
        return SlopeAngleOf(object) >= 0.0f || IsSolidObject(object);
    }

    const char* ObjectDisplayName(const NS::Obj::ObjectData& object)
    {
        if (!object.name.empty())
            return object.name.c_str();
        if (IsPlayerObject(object))
            return "Player";
        if (HasComponentType(object, "ThirdPersonFollow"))
            return "Follow Camera";
        if (HasComponentType(object, "PlacedVirtualCamera"))
            return "Camera";
        if (HasComponentType(object, "DirectionalLight"))
            return "Directional Light";
        if (HasComponentType(object, "PhysicsSettings"))
            return "Physics Settings";

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

        if (HasComponentType(object, "Hazard"))
            return "Hazard";
        if (MaterialRefOf(object) == "water")
            return "Water";
        if (HasComponentType(object, "SphereCollider"))
            return "Sphere";
        if (HasComponentType(object, "BoxCollider"))
            return "Solid";
        if (HasComponentType(object, "MeshRenderer"))
            return "Decoration";
        if (HasOnlyTransform(object))
            return "Empty";

        return "?";
    }

    const char* ObjectDisplayName(NS::Obj::GameObject& object)
    {
        if (!object.Name().empty())
            return object.Name().c_str();
        if (object.FindComponent<NS::Obj::PlayerInput>() != nullptr)
            return "Player";
        if (object.FindComponent<NS::Obj::ThirdPersonFollow>() != nullptr)
            return "Follow Camera";
        if (object.FindComponent<NS::Obj::PlacedVirtualCamera>() != nullptr)
            return "Camera";
        if (object.FindComponent<NS::Obj::DirectionalLight>() != nullptr)
            return "Directional Light";
        if (object.FindComponent<NS::Obj::PhysicsSettings>() != nullptr)
            return "Physics Settings";

        if (object.FindComponent<NS::Game::Level::Goal>() != nullptr)
            return "Goal";

        if (auto* slope = object.FindComponent<NS::Obj::SlopeCollider>())
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

        if (object.FindComponent<NS::Game::Level::Hazard>() != nullptr)
            return "Hazard";
        auto* mesh = object.FindComponent<NS::Obj::MeshRenderer>();
        if (mesh != nullptr && mesh->MaterialRef() == "water")
            return "Water";
        if (object.FindComponent<NS::Obj::SphereCollider>() != nullptr)
            return "Sphere";
        if (object.FindComponent<NS::Obj::BoxCollider>() != nullptr)
            return "Solid";
        if (mesh != nullptr)
            return "Decoration";
        // GameObject が必ず持つ TransformComponent 1 つだけなら中身が無い
        if (object.Components().size() == 1)
            return "Empty";

        return "?";
    }

    NS::Core::AABB PickLocalBounds(const NS::Obj::GameObject& object) noexcept
    {
        const auto* renderer = object.FindComponent<NS::Obj::MeshRenderer>();
        if (renderer != nullptr && renderer->GetMesh() != nullptr)
            return renderer->GetMesh()->LocalBounds();
        return NS::Core::AABB{NS::Core::Vector3{0.0f, 0.0f, 0.0f}, NS::Game::Level::k_CellHalfExtents};
    }

} // namespace NS::Editor
