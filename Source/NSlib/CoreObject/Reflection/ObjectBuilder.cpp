#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/Archetype.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace NS
{
    namespace
    {
        nlohmann::json MakeObjectHeaderJson(const SceneActor& obj, uint32 id)
        {
            nlohmann::json object = nlohmann::json::object();
            SetObjectJsonId(object, id);
            SetObjectJsonClass(object, obj.ClassName());
            SetObjectJsonName(object, obj.Name());
            SetObjectJsonActive(object, obj.IsAppeared());
            return object;
        }
    } // namespace

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
        // 部品構成はクラスの OnInit で決まる。クラスの無い JSON は配置物でない
        if (ObjectJsonClass(object).empty())
        {
            return nullptr;
        }
        return CreateRegisteredObject(object, scene);
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

    nlohmann::json ObjectToJson(const SceneActor& obj)
    {
        nlohmann::json object = MakeObjectHeaderJson(obj, obj.Id());
        nlohmann::json& parts = ObjectJsonClassObjs(object);
        ForEachReflectedClassObj(obj, [&](const ClassObjInfo& field, const Object* classObj) {
            parts[field.key] = SerializeClassObjFields(*classObj);
        });
        return DiffObjectJson(object);
    }
} // namespace NS
