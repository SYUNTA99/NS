#include "Editor/EditorObjects.h"

#include "Game/Level/Goal.h"
#include "Game/Level/KillZone.h"
#include "Game/Player.h"
#include "Runtime/Graphics/Mesh.h"
#include "Runtime/Object/Components/BoxCollider.h"
#include "Runtime/Object/Components/DirectionalLight.h"
#include "Runtime/Object/Components/PhysicsSettings.h"
#include "Runtime/Object/Components/MeshRenderer.h"
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

        bool HasComponentType(const nlohmann::json& object, const char* typeName) noexcept
        {
            return NS::Obj::FindComponentEntry(object, typeName) != nullptr;
        }

        // Transform しか持たない = 中身が無い GameObject。JSON 側は Transform も component 列に居る
        bool HasOnlyTransform(const nlohmann::json& object) noexcept
        {
            for (const nlohmann::json& entry : NS::Obj::ObjectJsonComponents(object))
            {
                if (NS::Obj::ComponentEntryType(entry) != "TransformComponent")
                    return false;
            }
            return true;
        }

        float SlopeAngleOf(const nlohmann::json& object) noexcept
        {
            const nlohmann::json* slope = NS::Obj::FindComponentEntry(object, "SlopeCollider");
            if (!slope)
                return -1.0f;
            return NS::Obj::FieldFloat(*slope, "角度 (度)", 0.0f);
        }

        std::string MaterialRefOf(const nlohmann::json& object)
        {
            const nlohmann::json* renderer = NS::Obj::FindComponentEntry(object, "MeshRenderer");
            if (!renderer)
                return {};
            return NS::Obj::FieldString(*renderer, "マテリアル", {});
        }
    } // namespace

    std::int16_t ObjectCellX(const nlohmann::json& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(NS::Obj::ObjectPosition(object).x));
    }

    std::int16_t ObjectCellY(const nlohmann::json& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(NS::Obj::ObjectPosition(object).y));
    }

    std::int16_t ObjectCellZ(const nlohmann::json& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(NS::Obj::ObjectPosition(object).z));
    }

    bool IsCellBrushObject(const nlohmann::json& object) noexcept
    {
        // 派生型の配置物は自前の組み立てを持つので、ブラシの置換や削除で崩さない
        return NS::Obj::ObjectJsonClass(object).empty() && NS::Obj::FindComponentEntry(object, "MeshRenderer") != nullptr;
    }

    bool IsCellBrushObject(NS::Obj::GameObject& object) noexcept
    {
        // 実行時の一時オブジェクトは配置物でないため対象外
        return !object.IsTransient() && std::string_view{object.ClassName()}.empty() &&
               object.FindComponent<NS::Obj::MeshRenderer>() != nullptr;
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

    std::size_t FindObjectAtCell(const nlohmann::json& scene,
                                 std::int16_t x,
                                 std::int16_t y,
                                 std::int16_t z) noexcept
    {
        const nlohmann::json& objects = NS::Obj::SceneJsonObjects(scene);
        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            const nlohmann::json& object = objects[i];
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

    std::uint8_t CellRotationStep(const nlohmann::json& object) noexcept
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

    void SetCellRotationStep(nlohmann::json& object, std::uint8_t rotationStep) noexcept
    {
        const float yaw = static_cast<float>(rotationStep & 0x03) * k_QuarterTurnYaw;
        const NS::Core::Quaternion rotation = NS::Core::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
        NS::Obj::SetObjectRotation(object, rotation);
    }

    nlohmann::json MakeMeshRendererEntry(std::string_view meshName,
                                         std::string_view materialName,
                                         const NS::Core::Vector3& baseColor)
    {
        nlohmann::json entry = NS::Obj::MakeComponentEntry("MeshRenderer");
        NS::Obj::SetField(entry, "メッシュ", meshName);
        NS::Obj::SetField(entry, "マテリアル", materialName);
        NS::Obj::SetField(entry, "基本色", baseColor);
        return entry;
    }

    nlohmann::json MakeCellCubeComponents()
    {
        // 1m 立方の cube mesh に当たりを合わせる
        nlohmann::json box = NS::Obj::MakeComponentEntry("BoxCollider");
        NS::Obj::SetField(box, "半径", NS::Core::Vector3{0.5f, 0.5f, 0.5f});
        // 色はテクスチャ未解決時のフォールバック
        return nlohmann::json::array(
            {MakeMeshRendererEntry("cube", "", NS::Core::Vector3{0.70f, 0.70f, 0.75f}), std::move(box)});
    }

    nlohmann::json MakeCellObject(std::int16_t x, std::int16_t y, std::int16_t z)
    {
        nlohmann::json object = NS::Obj::MakeObjectJson(MakeCellCubeComponents());
        // components を確定した後に transform を書き込む。先に書くと components 代入が TransformComponent を消す
        NS::Obj::SetObjectPosition(object,
                                   NS::Core::Vector3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
        return object;
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
        NS::Obj::SetField(slope, "半径", NS::Core::Vector3{0.5f, 0.5f, 0.5f});
        return nlohmann::json::array(
            {MakeMeshRendererEntry(meshName, "", NS::Core::Vector3{0.70f, 0.70f, 0.75f}),
             std::move(slope)});
    }

    nlohmann::json MakePrimitiveComponents(PrimitiveKind kind)
    {
        switch (kind)
        {
        case PrimitiveKind::Cube:
            return MakeCellCubeComponents();
        case PrimitiveKind::Sphere:
        {
            nlohmann::json sphere = NS::Obj::MakeComponentEntry("SphereCollider");
            NS::Obj::SetField(sphere, "半径", 0.5f);
            return nlohmann::json::array(
                {MakeMeshRendererEntry("sphere", "", NS::Core::Vector3{0.70f, 0.70f, 0.75f}),
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
            {MakeMeshRendererEntry("cube", "", NS::Core::Vector3{1.0f, 0.84f, 0.0f}),
             NS::Obj::MakeComponentEntry("Goal")});
    }

    bool IsSolidObject(const nlohmann::json& object)
    {
        const bool hasBox = HasComponentType(object, "BoxCollider");
        const bool hasSlope = HasComponentType(object, "SlopeCollider");
        const bool hasGoal = NS::Game::Level::IsGoalObject(object);
        const bool hasKillZone = NS::Game::Level::IsKillZoneObject(object);
        return hasBox && !hasSlope && !hasGoal && !hasKillZone;
    }

    bool IsRotatableObject(const nlohmann::json& object)
    {
        return SlopeAngleOf(object) >= 0.0f || IsSolidObject(object);
    }

    const char* ObjectDisplayName(const nlohmann::json& object)
    {
        // 名前は JSON の中の文字列を指す。std::string の中身なので終端がある
        const std::string_view name = NS::Obj::ObjectJsonName(object);
        if (!name.empty())
            return name.data();
        if (IsPlayerObject(object))
            return "Player";
        if (HasComponentType(object, "ThirdPersonFollow"))
            return "Follow Camera";
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
        if (object.FindComponent<NS::Obj::DirectionalLight>() != nullptr)
            return "Directional Light";
        if (object.FindComponent<NS::Obj::PhysicsSettings>() != nullptr)
            return "Physics Settings";

        if (object.FindComponent<NS::Game::Level::Goal>() != nullptr)
            return "Goal";

        if (NS::Obj::SlopeCollider* slope = object.FindComponent<NS::Obj::SlopeCollider>())
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

        NS::Obj::MeshRenderer* mesh = object.FindComponent<NS::Obj::MeshRenderer>();
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
        const NS::Obj::MeshRenderer* renderer = object.FindComponent<NS::Obj::MeshRenderer>();
        if (renderer != nullptr && renderer->GetMesh() != nullptr)
            return renderer->GetMesh()->LocalBounds();
        return NS::Core::AABB{NS::Core::Vector3{0.0f, 0.0f, 0.0f}, NS::Core::Vector3{0.5f, 0.5f, 0.5f}};
    }

} // namespace NS::Editor
