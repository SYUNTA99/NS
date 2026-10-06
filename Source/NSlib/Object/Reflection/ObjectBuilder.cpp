#include "NSlib/Object/Reflection/ObjectBuilder.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Component.h"
#include "NSlib/Object/Components/TransformComponent.h"
#include "NSlib/Object/Reflection/Archetype.h"
#include "NSlib/Object/Reflection/ComponentEntry.h"
#include "NSlib/Object/Reflection/Reflection.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"

#include <algorithm>
#include <string>
#include <vector>

namespace NS::Obj
{
    void ApplyObjectParts(Actor& obj, const nlohmann::json& object, PartCreation creation)
    {
        const nlohmann::json& parts = ObjectJsonParts(object);
        for (nlohmann::json::const_iterator entry = parts.begin(); entry != parts.end(); ++entry)
        {
            Component* target = obj.Part(entry.key());
            if (target == nullptr && creation == PartCreation::Allow)
            {
                target = obj.CreatePart(entry.key());
            }
            if (target == nullptr)
            {
                NS_LOG_WARN(Scene, "{} の部品 {} はクラスに無いので読み飛ばす", obj.ClassName(), entry.key());
                continue;
            }
            ApplyJsonFields(*target, entry.value());
        }
    }

    std::unique_ptr<Actor> ObjectFromJson(const nlohmann::json& object, AssetManager* assets)
    {
        // 部品はクラスのコンストラクタと種類の既定値が積む。クラスの無い JSON は配置物でない
        if (ObjectJsonClass(object).empty())
        {
            return nullptr;
        }

        // コードの既定値 < 種類の既定値 < 個体の上書き の順に重ねる
        std::unique_ptr<Actor> obj = CreateRegisteredObject(object);
        ApplyArchetype(*obj);
        ApplyObjectParts(*obj, object, PartCreation::Forbid);

        // 参照文字列の実体化は component 自身の仕事。AssetManager が無い間は文字列のまま持たせておく
        if (assets != nullptr)
        {
            obj->ForEachPart([assets](std::string_view, Component& part) { part.ResolveAssets(*assets); });
        }
        return obj;
    }

    nlohmann::json MakePrototypeJson(const Actor& obj)
    {
        nlohmann::json object = nlohmann::json::object();
        SetObjectJsonId(object, 0u);
        SetObjectJsonClass(object, obj.ClassName());
        SetObjectJsonName(object, obj.Name());
        SetObjectJsonActive(object, obj.IsActiveSelf());
        if (const Actor* parent = obj.Parent())
        {
            SetObjectJsonParent(object, parent->Id());
        }
        nlohmann::json& parts = ObjectJsonParts(object);
        obj.ForEachPart(
            [&parts](std::string_view name, Component&) { parts[std::string{name}] = nlohmann::json::object(); });
        SetObjectPosition(object, obj.Root().Position());
        SetObjectRotation(object, obj.Root().Rotation());
        SetObjectScale(object, obj.Root().Scale());
        return object;
    }

    nlohmann::json ObjectToJson(const Actor& obj)
    {
        nlohmann::json object = nlohmann::json::object();
        SetObjectJsonId(object, obj.Id());
        SetObjectJsonClass(object, obj.ClassName());
        SetObjectJsonName(object, obj.Name());
        SetObjectJsonActive(object, obj.IsActiveSelf());
        if (const Actor* parent = obj.Parent())
        {
            SetObjectJsonParent(object, parent->Id());
        }
        nlohmann::json& parts = ObjectJsonParts(object);
        obj.ForEachPart(
            [&parts](std::string_view name, Component& part) { parts[std::string{name}] = SerializePartFields(part); });
        return DiffObjectJson(object);
    }
} // namespace NS::Obj
