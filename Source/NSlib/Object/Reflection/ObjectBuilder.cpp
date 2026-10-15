#include "NSlib/Object/Reflection/ObjectBuilder.h"
#include "NSlib/Object/Reflection/ReflectedSubObjs.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Object/SceneActor.h"
#include "NSlib/Object/Reflection/Archetype.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/SubObject.h"
#include "NSlib/Object/SubObjects/Transform.h"

#include <string>

namespace NS::Obj
{
    namespace
    {
        nlohmann::json MakeObjectHeaderJson(const SceneActor& obj, NS::uint32 id)
        {
            nlohmann::json object = nlohmann::json::object();
            SetObjectJsonId(object, id);
            SetObjectJsonClass(object, obj.ClassName());
            SetObjectJsonName(object, obj.Name());
            SetObjectJsonActive(object, obj.IsAppeared());
            if (const SceneActor* parent = obj.Parent())
            {
                SetObjectJsonParent(object, parent->Id());
            }
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

    std::unique_ptr<SceneActor> ObjectFromJson(const nlohmann::json& object, AssetManager* assets)
    {
        // 部品構成はクラスの OnInit で決まる。クラスの無い JSON は配置物でない
        if (ObjectJsonClass(object).empty())
        {
            return nullptr;
        }

        // コードの既定値 < 種類の既定値 < 個体の上書き の順に重ねる
        std::unique_ptr<SceneActor> obj = CreateRegisteredObject(object);
        ApplyArchetype(*obj);
        ApplyObjectSubObjs(*obj, object);

        // 参照文字列の実体化は SubObject 自身の仕事。AssetManager が無い間は文字列のまま持たせておく
        if (assets != nullptr)
        {
            ForEachReflectedSubObj(*obj, [&](const SubObjectDesc&, SubObject* subObject) {
                subObject->ResolveAssets(*assets);
            });
        }
        return obj;
    }

    nlohmann::json MakePrototypeJson(const SceneActor& obj)
    {
        nlohmann::json object = MakeObjectHeaderJson(obj, 0u);
        nlohmann::json& parts = ObjectJsonSubObjs(object);
        ForEachReflectedSubObj(obj, [&](const SubObjectDesc& field, SubObject*) {
            parts[field.key] = nlohmann::json::object();
        });
        SetObjectPosition(object, obj.Root().Position());
        SetObjectRotation(object, obj.Root().Rotation());
        SetObjectScale(object, obj.Root().Scale());
        return object;
    }

    nlohmann::json ObjectToJson(const SceneActor& obj)
    {
        nlohmann::json object = MakeObjectHeaderJson(obj, obj.Id());
        nlohmann::json& parts = ObjectJsonSubObjs(object);
        ForEachReflectedSubObj(obj, [&](const NS::Obj::SubObjectDesc& field, const SubObject* subObject) {
            parts[field.key] = SerializeSubObjFields(*subObject);
        });
        return DiffObjectJson(object);
    }
} // namespace NS::Obj
