#include "Editor/EditorActors.h"
#include "NSlib/Core/Assert.h"
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
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>
#include <unordered_set>

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

        std::optional<nlohmann::json> CaptureActor(ActorList& actors, uint32 id)
        {
            const SceneActor* actor = actors.FindByActorId(id);
            if (actor == nullptr)
            {
                return std::nullopt;
            }
            // 写しから同じ姿を作り直せるように、アクターが自分を全部品の値まで書き出す
            return ActorToJson(actor);
        }

        nlohmann::json AllFieldValues(const SceneActor& actor)
        {
            nlohmann::json values = nlohmann::json::object();
            nlohmann::json& parts = ActorJsonClassObjs(values);
            ForEachReflectedClassObj(&actor, [&](const ClassObjInfo& row, const Object* part) {
                parts[std::string{row.key}] = SerializeClassObjFields(part);
            });
            return values;
        }

        bool AddChangedSceneOverrides(SceneActor& actor, const nlohmann::json& before)
        {
            bool changed = false;
            ForEachReflectedClassObj(&actor, [&](const ClassObjInfo& row, const Object* part) {
                const nlohmann::json now = SerializeClassObjFields(part);
                const nlohmann::json* was = ClassObjFields(before, row.key);
                for (nlohmann::json::const_iterator entry = now.begin(); entry != now.end(); ++entry)
                {
                    // 触ったかを見る比べなので、値の写しどうしを == で比べる
                    if (was != nullptr && was->contains(entry.key()) && was->at(entry.key()) == entry.value())
                    {
                        continue;
                    }
                    const FieldDesc* field = FindField(part->GetReflection(), entry.key());
                    if (field == nullptr || FieldDestinationOf(&actor, *field) != FieldDestination::Scene)
                    {
                        continue;
                    }
                    changed = true;
                    if (field->type != FieldType::ActorRef)
                    {
                        actor.AddSceneOverride(row.key, entry.key());
                    }
                }
            });
            return changed;
        }

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

        nlohmann::json MakePrototypeJson(const SceneActor* obj)
        {
            NS_ASSERT(App, obj != nullptr, "MakePrototypeJson: アクターが null");
            nlohmann::json object = MakeActorHeaderJson(obj, 0u);
            nlohmann::json& parts = ActorJsonClassObjs(object);
            ForEachReflectedClassObj(
                obj, [&](const ClassObjInfo& field, Object*) { parts[field.key] = nlohmann::json::object(); });
            SetActorJsonPosition(object, obj->TransformObj().Position());
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

        std::vector<uint32> CollectWithDescendants(const ActorList& actors, const std::vector<uint32>& roots)
        {
            std::vector<uint32> collected;
            std::unordered_set<uint32> seen;
            for (const uint32 root : roots)
            {
                if (root != k_NoActorId && seen.insert(root).second)
                {
                    collected.push_back(root);
                }
            }
            // 入れた順に子を探して後ろへ足す。seen に入った id は二度入らないので、輪でも止まる
            for (std::size_t i = 0; i < collected.size(); ++i)
            {
                for (const SceneActor* object : actors)
                {
                    if (object->TransformObj().Parent().id == collected[i] && seen.insert(object->Id()).second)
                    {
                        collected.push_back(object->Id());
                    }
                }
            }
            return collected;
        }

        bool CanReparent(const ActorList& actors, uint32 child, uint32 parent)
        {
            if (parent == k_NoActorId)
            {
                return true;
            }
            const std::vector<uint32> family = CollectWithDescendants(actors, {child});
            return std::find(family.begin(), family.end(), parent) == family.end();
        }

        bool IsCellBrushActor(const SceneActor* object) noexcept
        {
            NS_ASSERT(App, object != nullptr, "IsCellBrushActor: アクターが null");
            // 自前の振る舞いを持つ SceneActor は、ブラシの置換や削除で崩さない
            if (std::string_view{object->ClassName()} != "MapPartsActor")
            {
                return false;
            }
            const Model* model = object->ModelClassObj();
            if (model == nullptr)
            {
                return false;
            }
            const std::string_view mesh = model->MeshRef();
            return mesh == "cube" || mesh == "wedge45";
        }

        int16 ActorCellX(const SceneActor* object) noexcept
        {
            NS_ASSERT(App, object != nullptr, "ActorCellX: アクターが null");
            return static_cast<int16>(std::lround(object->TransformObj().Position().x));
        }

        int16 ActorCellY(const SceneActor* object) noexcept
        {
            NS_ASSERT(App, object != nullptr, "ActorCellY: アクターが null");
            return static_cast<int16>(std::lround(object->TransformObj().Position().y));
        }

        int16 ActorCellZ(const SceneActor* object) noexcept
        {
            NS_ASSERT(App, object != nullptr, "ActorCellZ: アクターが null");
            return static_cast<int16>(std::lround(object->TransformObj().Position().z));
        }

        uint32 FindActorIdAtCell(const ActorList& objects, int16 x, int16 y, int16 z) noexcept
        {
            for (const SceneActor* object : PlacedActors(objects))
            {
                if (IsCellBrushActor(object) && ActorCellX(object) == x && ActorCellY(object) == y &&
                    ActorCellZ(object) == z)
                {
                    return object->Id();
                }
            }
            return k_NoActorId;
        }

        bool HasPlacedActorAtCell(const ActorList& objects, int16 x, int16 y, int16 z) noexcept
        {
            for (const SceneActor* object : PlacedActors(objects))
            {
                if (ActorCellX(object) == x && ActorCellY(object) == y && ActorCellZ(object) == z)
                {
                    return true;
                }
            }
            return false;
        }

        void TurnCellQuarter(SceneActor* object) noexcept
        {
            NS_ASSERT(App, object != nullptr, "TurnCellQuarter: アクターが null");
            // 今の回転の後に世界の Y 軸まわりを掛ける。段へ丸めると傾きが消える
            const Quaternion quarter = Quaternion::CreateFromYawPitchRoll(k_QuarterTurnYaw, 0.0f, 0.0f);
            Transform& transform = object->TransformObj();
            transform.SetRotation(transform.Rotation() * quarter);
        }

        void SetCellRotationStep(nlohmann::json& actorJson, uint8 rotationStep) noexcept
        {
            const float yaw = static_cast<float>(rotationStep & 0x03) * k_QuarterTurnYaw;
            const Quaternion rotation = Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
            SetActorJsonRotation(actorJson, rotation);
        }

        const char* ActorDisplayName(const SceneActor* object)
        {
            NS_ASSERT(App, object != nullptr, "ActorDisplayName: アクターが null");
            if (!object->Name().empty())
            {
                return object->Name().c_str();
            }
            return ClassDisplayName(object->ClassName());
        }

        AABB PickLocalBounds(const SceneActor* object) noexcept
        {
            NS_ASSERT(App, object != nullptr, "PickLocalBounds: アクターが null");
            const Model* renderer = object->ModelClassObj();
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
