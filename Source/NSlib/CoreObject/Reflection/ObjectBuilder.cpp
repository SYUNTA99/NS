#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NS
{
    nlohmann::json MakeObjectHeaderJson(const SceneActor& obj, uint32 id)
    {
        nlohmann::json object = nlohmann::json::object();
        WriteObjectHeader(
            object,
            ObjectHeader{.id = id, .actorFile = obj.ActorFileName(), .name = obj.Name(), .active = obj.IsAppeared()});
        return object;
    }

    void ApplyObjectClassObjs(SceneActor& obj, const nlohmann::json& object)
    {
        const nlohmann::json& parts = ObjectJsonClassObjs(object);
        std::vector<std::string_view> knownKeys;
        ForEachReflectedClassObj(obj, [&](const ClassObjInfo& field, Object* classObj) {
            knownKeys.push_back(field.key);
            const nlohmann::json::const_iterator entry = parts.find(field.key);
            if (entry != parts.end())
            {
                ApplyJsonFields(*classObj, entry.value());
            }
        });
        for (nlohmann::json::const_iterator entry = parts.begin(); entry != parts.end(); ++entry)
        {
            if (std::find(knownKeys.begin(), knownKeys.end(), entry.key()) == knownKeys.end())
            {
                NS_LOG_WARN(Scene, "{} の部品 {} はクラスに無いので読み飛ばす", obj.ClassName(), entry.key());
            }
        }
    }

    std::unique_ptr<SceneActor> ObjectFromJson(const nlohmann::json& object, Scene& scene)
    {
        return CreateRegisteredObject(object, scene);
    }

    nlohmann::json ObjectToJson(const SceneActor& obj)
    {
        nlohmann::json object = MakeObjectHeaderJson(obj, obj.Id());
        nlohmann::json& parts = ObjectJsonClassObjs(object);
        // 値は .actor と比べない。シーンに書くのは参照の欄と上書きの一覧の項目で、ほかは .actor の値を使う
        ForEachReflectedClassObj(obj, [&](const ClassObjInfo& row, const Object* classObj) {
            nlohmann::json fields = SerializeClassObjFields(*classObj);
            for (nlohmann::json::iterator entry = fields.begin(); entry != fields.end();)
            {
                const FieldDesc* field = FindField(classObj->GetReflection(), entry.key());
                const bool reference = field != nullptr && field->type == FieldType::ActorRef;
                if (reference || obj.IsSceneOverride(row.key, entry.key()))
                {
                    ++entry;
                    continue;
                }
                entry = fields.erase(entry);
            }
            parts[row.key] = std::move(fields);
        });
        return object;
    }
} // namespace NS
