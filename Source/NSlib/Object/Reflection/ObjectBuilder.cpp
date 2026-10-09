#include "NSlib/Object/Reflection/ObjectBuilder.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/SubObject.h"
#include "NSlib/Object/SubObjects/TransformSubObject.h"
#include "NSlib/Object/Reflection/Archetype.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"

#include <string>

namespace NS::Obj
{
    namespace
    {
        nlohmann::json MakeObjectHeaderJson(const Actor& obj, std::uint32_t id)
        {
            nlohmann::json object = nlohmann::json::object();
            SetObjectJsonId(object, id);
            SetObjectJsonClass(object, obj.ClassName());
            SetObjectJsonName(object, obj.Name());
            SetObjectJsonActive(object, obj.IsActiveSelf());
            if (const Actor* parent = obj.Parent())
            {
                SetObjectJsonParent(object, parent->Id());
            }
            return object;
        }
    } // namespace

    void ApplyObjectSubObjs(Actor& obj, const nlohmann::json& object, SubObjCreation creation)
    {
        const nlohmann::json& parts = ObjectJsonSubObjs(object);
        for (nlohmann::json::const_iterator entry = parts.begin(); entry != parts.end(); ++entry)
        {
            SubObject* target = obj.FindSubObj(entry.key());
            if (target == nullptr && creation == SubObjCreation::Allow)
            {
                target = obj.CreateSubObj(entry.key());
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
        // 部品はクラスの Init と種類の既定値が積む。クラスの無い JSON は配置物でない
        if (ObjectJsonClass(object).empty())
        {
            return nullptr;
        }

        // コードの既定値 < 種類の既定値 < 個体の上書き の順に重ねる
        std::unique_ptr<Actor> obj = CreateRegisteredObject(object);
        ApplyArchetype(*obj);
        ApplyObjectSubObjs(*obj, object, SubObjCreation::Forbid);

        // 参照文字列の実体化は SubObject 自身の仕事。AssetManager が無い間は文字列のまま持たせておく
        if (assets != nullptr)
        {
            for (SubObject* subObject : obj->SubObjs())
            {
                subObject->ResolveAssets(*assets);
            }
        }
        return obj;
    }

    nlohmann::json MakePrototypeJson(const Actor& obj)
    {
        nlohmann::json object = MakeObjectHeaderJson(obj, 0u);
        nlohmann::json& parts = ObjectJsonSubObjs(object);
        for (const SubObject* subObject : obj.SubObjs())
        {
            parts[subObject->Name()] = nlohmann::json::object();
        }
        SetObjectPosition(object, obj.Root().Position());
        SetObjectRotation(object, obj.Root().Rotation());
        SetObjectScale(object, obj.Root().Scale());
        return object;
    }

    nlohmann::json ObjectToJson(const Actor& obj)
    {
        nlohmann::json object = MakeObjectHeaderJson(obj, obj.Id());
        nlohmann::json& parts = ObjectJsonSubObjs(object);
        for (const SubObject* subObject : obj.SubObjs())
        {
            parts[subObject->Name()] = SerializeSubObjFields(*subObject);
        }
        return DiffObjectJson(object);
    }
} // namespace NS::Obj
