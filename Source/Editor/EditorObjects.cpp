#include "Editor/EditorObjects.h"
#include "NSlib/Core/Types.h"

#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
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

        nlohmann::json& EnsureTransformClassObj(nlohmann::json& object)
        {
            nlohmann::json& transform = ObjectJsonClassObjs(object)["Transform"];
            if (!transform.is_object())
            {
                transform = nlohmann::json::object();
                SetField(transform, k_PositionFieldName, Vector3{0.0f, 0.0f, 0.0f});
                SetField(transform, k_RotationFieldName, Quaternion::Identity);
                SetField(transform, k_ScaleFieldName, Vector3{1.0f, 1.0f, 1.0f});
            }
            return transform;
        }

        Vector3 ObjectPosition(const nlohmann::json& object) noexcept
        {
            return ReadTransformVec3(object, k_PositionFieldName, Vector3{0.0f, 0.0f, 0.0f});
        }

        void SetObjectPosition(nlohmann::json& object, const Vector3& position) noexcept
        {
            SetField(EnsureTransformClassObj(object), k_PositionFieldName, position);
        }

        Quaternion ObjectRotation(const nlohmann::json& object) noexcept
        {
            const nlohmann::json* transform = FindTransformFields(object);
            if (transform == nullptr)
            {
                return Quaternion::Identity;
            }
            return FieldQuaternion(*transform, k_RotationFieldName, Quaternion::Identity);
        }

        void SetObjectRotation(nlohmann::json& object, const Quaternion& rotation) noexcept
        {
            SetField(EnsureTransformClassObj(object), k_RotationFieldName, rotation);
        }

        Vector3 ObjectScale(const nlohmann::json& object) noexcept
        {
            return ReadTransformVec3(object, k_ScaleFieldName, Vector3{1.0f, 1.0f, 1.0f});
        }

        void SetObjectScale(nlohmann::json& object, const Vector3& scale) noexcept
        {
            SetField(EnsureTransformClassObj(object), k_ScaleFieldName, scale);
        }

        nlohmann::json MakePrototypeJson(const SceneActor& obj)
        {
            nlohmann::json object = MakeObjectHeaderJson(obj, 0u);
            nlohmann::json& parts = ObjectJsonClassObjs(object);
            ForEachReflectedClassObj(
                obj, [&](const ClassObjInfo& field, Object*) { parts[field.key] = nlohmann::json::object(); });
            SetObjectPosition(object, obj.Root().Position());
            SetObjectRotation(object, obj.Root().Rotation());
            SetObjectScale(object, obj.Root().Scale());
            return object;
        }

        nlohmann::json MakePrototypeJson(std::string_view actorFileName)
        {
            nlohmann::json object = MakeObjectJson();
            SetObjectJsonActorFile(object, actorFileName);
            const Transform initial;
            SetObjectPosition(object, initial.Position());
            SetObjectRotation(object, initial.Rotation());
            SetObjectScale(object, initial.Scale());
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

            bool IsPlayerObject(const nlohmann::json& object)
            {
                return ActorFileLibrary::Get().ClassNameOf(ObjectJsonActorFile(object)) ==
                       ::PlayerActor::StaticReflection()->typeName;
            }
        } // namespace

        std::vector<SceneActor*> PlacedActors(const ActorList& objects)
        {
            std::vector<SceneActor*> placed;
            placed.reserve(objects.ObjectCount());
            for (SceneActor* object : objects)
            {
                placed.push_back(object);
            }
            return placed;
        }

        bool IsCellBrushObject(const SceneActor& object) noexcept
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

        int16 ObjectCellX(const SceneActor& object) noexcept
        {
            return static_cast<int16>(std::lround(object.Root().Position().x));
        }

        int16 ObjectCellY(const SceneActor& object) noexcept
        {
            return static_cast<int16>(std::lround(object.Root().Position().y));
        }

        int16 ObjectCellZ(const SceneActor& object) noexcept
        {
            return static_cast<int16>(std::lround(object.Root().Position().z));
        }

        uint32 FindObjectIdAtCell(const ActorList& objects, int16 x, int16 y, int16 z) noexcept
        {
            for (SceneActor* objPtr : PlacedActors(objects))
            {
                SceneActor& object = *objPtr;
                if (IsCellBrushObject(object) && ObjectCellX(object) == x && ObjectCellY(object) == y &&
                    ObjectCellZ(object) == z)
                {
                    return object.Id();
                }
            }
            return k_NoObjectId;
        }

        bool HasPlacedObjectAtCell(const ActorList& objects, int16 x, int16 y, int16 z) noexcept
        {
            for (const SceneActor* object : PlacedActors(objects))
            {
                if (ObjectCellX(*object) == x && ObjectCellY(*object) == y && ObjectCellZ(*object) == z)
                {
                    return true;
                }
            }
            return false;
        }

        void AddCellQuarterTurn(nlohmann::json& object) noexcept
        {
            // 今の回転の後に世界の Y 軸まわりを掛ける。段へ丸めると傾きが消える
            const Quaternion quarter = Quaternion::CreateFromYawPitchRoll(k_QuarterTurnYaw, 0.0f, 0.0f);
            SetObjectRotation(object, ObjectRotation(object) * quarter);
        }

        void SetCellRotationStep(nlohmann::json& object, uint8 rotationStep) noexcept
        {
            const float yaw = static_cast<float>(rotationStep & 0x03) * k_QuarterTurnYaw;
            const Quaternion rotation = Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
            SetObjectRotation(object, rotation);
        }

        const char* ObjectDisplayName(const SceneActor& object)
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

        std::size_t FindPlayerObjectIndex(const nlohmann::json& scene) noexcept
        {
            const nlohmann::json& objects = SceneJsonObjects(scene);
            for (std::size_t i = 0; i < objects.size(); ++i)
            {
                if (IsPlayerObject(objects[i]))
                {
                    return i;
                }
            }
            return k_NoObjectIndex;
        }

        nlohmann::json MakePlayerObject(const Vector3& position, const Quaternion& rotation)
        {
            nlohmann::json object = MakePrototypeJson<::PlayerActor>();
            SetObjectPosition(object, position);
            SetObjectRotation(object, rotation);
            return object;
        }
    } // namespace Editor
} // namespace NS
