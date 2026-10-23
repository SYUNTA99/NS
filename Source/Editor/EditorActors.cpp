#include "Editor/EditorActors.h"
#include "NSlib/Core/Types.h"

#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/CoreObject/Reflection/ActorBuilder.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace NS
{
    namespace Editor
    {
        namespace
        {
            const nlohmann::json* FindTransformFields(const nlohmann::json& object) noexcept
            {
                return ClassObjFields(object, "Transform");
            }

            Vector3 ReadTransformVec3(const nlohmann::json& object,
                                      std::string_view fieldName,
                                      const Vector3& fallback) noexcept
            {
                const nlohmann::json* transform = FindTransformFields(object);
                if (transform == nullptr)
                {
                    return fallback;
                }
                return FieldVector3(*transform, fieldName, fallback);
            }
        } // namespace

        nlohmann::json& EnsureTransformClassObj(nlohmann::json& actorJson)
        {
            nlohmann::json& transform = ActorJsonClassObjs(actorJson)["Transform"];
            if (!transform.is_object())
            {
                transform = nlohmann::json::object();
            }
            return transform;
        }

        Vector3 ActorJsonPosition(const nlohmann::json& actorJson) noexcept
        {
            return ReadTransformVec3(actorJson, k_PositionFieldName, Vector3{0.0f, 0.0f, 0.0f});
        }

        void SetActorJsonPosition(nlohmann::json& actorJson, const Vector3& position) noexcept
        {
            SetField(EnsureTransformClassObj(actorJson), k_PositionFieldName, position);
        }

        Quaternion ActorJsonRotation(const nlohmann::json& actorJson) noexcept
        {
            const nlohmann::json* transform = FindTransformFields(actorJson);
            if (transform == nullptr)
            {
                return Quaternion::Identity;
            }
            return FieldQuaternion(*transform, k_RotationFieldName, Quaternion::Identity);
        }

        void SetActorJsonRotation(nlohmann::json& actorJson, const Quaternion& rotation) noexcept
        {
            SetField(EnsureTransformClassObj(actorJson), k_RotationFieldName, rotation);
        }

        Vector3 ActorJsonScale(const nlohmann::json& actorJson) noexcept
        {
            return ReadTransformVec3(actorJson, k_ScaleFieldName, Vector3{1.0f, 1.0f, 1.0f});
        }

        void SetActorJsonScale(nlohmann::json& actorJson, const Vector3& scale) noexcept
        {
            SetField(EnsureTransformClassObj(actorJson), k_ScaleFieldName, scale);
        }

        nlohmann::json MakePrototypeJson(const SceneActor& obj)
        {
            nlohmann::json object = MakeActorHeaderJson(obj, 0u);
            nlohmann::json& parts = ActorJsonClassObjs(object);
            ForEachReflectedClassObj(
                obj, [&](const ClassObjInfo& field, Object*) { parts[field.key] = nlohmann::json::object(); });
            SetActorJsonPosition(object, obj.Root().Position());
            return object;
        }

        nlohmann::json MakePrototypeJson(std::string_view actorFileName)
        {
            nlohmann::json object = MakeActorJson();
            SetActorJsonFile(object, actorFileName);
            const Transform initial;
            SetActorJsonPosition(object, initial.Position());
            return object;
        }

        namespace
        {
            // cell ブラシの回転値 0..3 を Y 軸 90° 刻みの yaw ラジアンへ写す係数
            constexpr float k_QuarterTurnYaw = k_Pi * 0.5f;

            // 登録の表示名、なければクラス名。未登録は SceneActor。返す文字列は登録情報が所有する
            const char* ClassDisplayName(std::string_view className) noexcept
            {
                if (className.empty())
                {
                    return "SceneActor";
                }
                const TypeRegistry::Entry* entry = TypeRegistry::Get().Find(className);
                if (entry == nullptr)
                {
                    return "SceneActor";
                }
                if (entry->label != nullptr)
                {
                    return entry->label;
                }
                return entry->className;
            }

            bool IsPlayerActorJson(const nlohmann::json& object)
            {
                return ActorFileLibrary::Get().ClassNameOf(ActorJsonFile(object)) ==
                       ::PlayerActor::StaticReflection()->typeName;
            }
        } // namespace

        std::vector<SceneActor*> PlacedActors(const ActorList& objects)
        {
            std::vector<SceneActor*> placed;
            placed.reserve(objects.ActorCount());
            for (SceneActor* object : objects)
            {
                placed.push_back(object);
            }
            return placed;
        }

        bool IsCellBrushActor(const SceneActor& object) noexcept
        {
            // 自前の振る舞いを持つ SceneActor は、ブラシの置換や削除で崩さない
            if (std::string_view{object.ClassName()} != "MapPartsActor")
            {
                return false;
            }
            const Model* model = object.ModelClassObj();
            if (model == nullptr)
            {
                return false;
            }
            const std::string_view mesh = model->MeshRef();
            return mesh == "cube" || mesh == "wedge45";
        }

        int16 ActorCellX(const SceneActor& object) noexcept
        {
            return static_cast<int16>(std::lround(object.Root().Position().x));
        }

        int16 ActorCellY(const SceneActor& object) noexcept
        {
            return static_cast<int16>(std::lround(object.Root().Position().y));
        }

        int16 ActorCellZ(const SceneActor& object) noexcept
        {
            return static_cast<int16>(std::lround(object.Root().Position().z));
        }

        uint32 FindActorIdAtCell(const ActorList& objects, int16 x, int16 y, int16 z) noexcept
        {
            for (SceneActor* objPtr : PlacedActors(objects))
            {
                SceneActor& object = *objPtr;
                if (IsCellBrushActor(object) && ActorCellX(object) == x && ActorCellY(object) == y &&
                    ActorCellZ(object) == z)
                {
                    return object.Id();
                }
            }
            return k_NoActorId;
        }

        bool HasPlacedActorAtCell(const ActorList& objects, int16 x, int16 y, int16 z) noexcept
        {
            for (const SceneActor* object : PlacedActors(objects))
            {
                if (ActorCellX(*object) == x && ActorCellY(*object) == y && ActorCellZ(*object) == z)
                {
                    return true;
                }
            }
            return false;
        }

        void AddCellQuarterTurn(nlohmann::json& actorJson) noexcept
        {
            // 今の回転の後に世界の Y 軸まわりを掛ける。段へ丸めると傾きが消える
            const Quaternion quarter = Quaternion::CreateFromYawPitchRoll(k_QuarterTurnYaw, 0.0f, 0.0f);
            SetActorJsonRotation(actorJson, ActorJsonRotation(actorJson) * quarter);
        }

        void SetCellRotationStep(nlohmann::json& actorJson, uint8 rotationStep) noexcept
        {
            const float yaw = static_cast<float>(rotationStep & 0x03) * k_QuarterTurnYaw;
            const Quaternion rotation = Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
            SetActorJsonRotation(actorJson, rotation);
        }

        const char* ActorDisplayName(const SceneActor& object)
        {
            if (!object.Name().empty())
            {
                return object.Name().c_str();
            }
            return ClassDisplayName(object.ClassName());
        }

        AABB PickLocalBounds(const SceneActor& object) noexcept
        {
            const Model* renderer = object.ModelClassObj();
            if (renderer != nullptr && renderer->GetMesh()->LocalBounds() != nullptr)
            {
                return *renderer->GetMesh()->LocalBounds();
            }
            return AABB{Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.5f, 0.5f, 0.5f}};
        }

        std::size_t FindPlayerActorIndex(const nlohmann::json& scene) noexcept
        {
            const nlohmann::json& objects = SceneJsonActors(scene);
            for (std::size_t i = 0; i < objects.size(); ++i)
            {
                if (IsPlayerActorJson(objects[i]))
                {
                    return i;
                }
            }
            return k_NoActorIndex;
        }
    } // namespace Editor
} // namespace NS
