#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Reflection/ReflectedSubObjs.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/Reflection/Archetype.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObject.h"
#include "NSlib/CoreObject/SubObjects/Transform.h"

#include <string>

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

    void ApplyObjectSubObjs(SceneActor& obj, const nlohmann::json& object)
    {
        const nlohmann::json& parts = ObjectJsonSubObjs(object);
        for (nlohmann::json::const_iterator entry = parts.begin(); entry != parts.end(); ++entry)
        {
            SubObject* target = obj.FindSubObj(entry.key());
            if (target == nullptr)
            {
                NS_LOG_WARN(Scene, "{} の部品 {} はクラスに無いので読み飛ばす", obj.ClassName(), entry.key());
                continue;
            }
            ApplyJsonFields(*target, entry.value());
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
        nlohmann::json& parts = ObjectJsonSubObjs(object);
        ForEachReflectedSubObj(
            obj, [&](const SubObjectDesc& field, SubObject*) { parts[field.key] = nlohmann::json::object(); });
        SetObjectPosition(object, obj.Root().Position());
        SetObjectRotation(object, obj.Root().Rotation());
        SetObjectScale(object, obj.Root().Scale());
        return object;
    }

    nlohmann::json ObjectToJson(const SceneActor& obj)
    {
        nlohmann::json object = MakeObjectHeaderJson(obj, obj.Id());
        nlohmann::json& parts = ObjectJsonSubObjs(object);
        ForEachReflectedSubObj(obj, [&](const SubObjectDesc& field, const SubObject* subObject) {
            parts[field.key] = SerializeSubObjectFields(*subObject);
        });
        return DiffObjectJson(object);
    }
} // namespace NS
