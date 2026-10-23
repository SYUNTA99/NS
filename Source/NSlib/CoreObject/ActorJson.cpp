#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/Core/Types.h"

#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"

#include <string>

namespace NS
{
    const nlohmann::json& ActorJsonClassObjs(const nlohmann::json& actorJson) noexcept
    {
        static const nlohmann::json empty = nlohmann::json::object();
        if (!actorJson.is_object())
        {
            return empty;
        }
        const nlohmann::json::const_iterator found = actorJson.find("classObjs");
        if (found == actorJson.end() || !found->is_object())
        {
            return empty;
        }
        return *found;
    }

    nlohmann::json& ActorJsonClassObjs(nlohmann::json& actorJson)
    {
        if (!actorJson.is_object())
        {
            actorJson = nlohmann::json::object();
        }
        nlohmann::json& parts = actorJson["classObjs"];
        if (!parts.is_object())
        {
            parts = nlohmann::json::object();
        }
        return parts;
    }

    const nlohmann::json* ClassObjFields(const nlohmann::json& actorJson, std::string_view partName) noexcept
    {
        const nlohmann::json& parts = ActorJsonClassObjs(actorJson);
        const nlohmann::json::const_iterator found = parts.find(std::string{partName});
        if (found == parts.end() || !found->is_object())
        {
            return nullptr;
        }
        return &*found;
    }

    nlohmann::json* ClassObjFields(nlohmann::json& actorJson, std::string_view partName) noexcept
    {
        const nlohmann::json* found = ClassObjFields(static_cast<const nlohmann::json&>(actorJson), partName);
        if (found == nullptr)
        {
            return nullptr;
        }
        return &ActorJsonClassObjs(actorJson)[std::string{partName}];
    }
    namespace
    {
        // object の key が文字列なら返す。無いか壊れていれば空
        [[nodiscard]] std::string_view ReadString(const nlohmann::json& object, const char* key) noexcept
        {
            if (!object.is_object())
            {
                return {};
            }
            const nlohmann::json::const_iterator it = object.find(key);
            if (it == object.end() || !it->is_string())
            {
                return {};
            }
            return it->get_ref<const std::string&>();
        }

        // 既定値は書かずに消す。保存の差分を値の変わった欄だけにする
        void WriteOrErase(nlohmann::json& object, const char* key, bool isDefault, nlohmann::json value)
        {
            if (!object.is_object())
            {
                object = nlohmann::json::object();
            }
            if (isDefault)
            {
                object.erase(key);
                return;
            }
            object[key] = std::move(value);
        }
    } // namespace

    nlohmann::json MakeActorJson()
    {
        nlohmann::json object = nlohmann::json::object();
        object["id"] = 0u;
        object["classObjs"] = nlohmann::json::object();
        return object;
    }

    uint32 ActorJsonId(const nlohmann::json& actorJson) noexcept
    {
        return FieldUnsigned(actorJson, "id", 0);
    }

    void SetActorJsonId(nlohmann::json& actorJson, uint32 id)
    {
        WriteOrErase(actorJson, "id", false, id);
    }

    std::string_view ActorJsonFile(const nlohmann::json& actorJson) noexcept
    {
        return ReadString(actorJson, "actor");
    }

    void SetActorJsonFile(nlohmann::json& actorJson, std::string_view actorFile)
    {
        WriteOrErase(actorJson, "actor", actorFile.empty(), std::string{actorFile});
    }

    std::string_view ActorJsonName(const nlohmann::json& actorJson) noexcept
    {
        return ReadString(actorJson, "name");
    }

    void SetActorJsonName(nlohmann::json& actorJson, std::string_view name)
    {
        WriteOrErase(actorJson, "name", name.empty(), std::string{name});
    }

    bool ActorJsonActive(const nlohmann::json& actorJson) noexcept
    {
        if (!actorJson.is_object())
        {
            return true;
        }
        const nlohmann::json::const_iterator it = actorJson.find("active");
        if (it == actorJson.end() || !it->is_boolean())
        {
            return true;
        }
        return it->get<bool>();
    }

    void SetActorJsonActive(nlohmann::json& actorJson, bool active)
    {
        WriteOrErase(actorJson, "active", active, active);
    }

    ActorHeader ReadActorHeader(const nlohmann::json& actorJson)
    {
        return ActorHeader{.id = ActorJsonId(actorJson),
                            .actorFile = std::string{ActorJsonFile(actorJson)},
                            .name = std::string{ActorJsonName(actorJson)},
                            .active = ActorJsonActive(actorJson)};
    }

    void WriteActorHeader(nlohmann::json& actorJson, const ActorHeader& header)
    {
        SetActorJsonId(actorJson, header.id);
        SetActorJsonFile(actorJson, header.actorFile);
        SetActorJsonName(actorJson, header.name);
        SetActorJsonActive(actorJson, header.active);
    }

    namespace
    {
        constexpr const char* k_RefKey = "ref";
    } // namespace

    bool IsRefValue(const nlohmann::json& value) noexcept
    {
        return value.is_object() && value.contains(k_RefKey);
    }

    nlohmann::json MakeRefValue(ActorRef ref)
    {
        nlohmann::json value = nlohmann::json::object();
        value[k_RefKey] = ref.id;
        return value;
    }

    bool ReadRefValue(const nlohmann::json& value, ActorRef& out) noexcept
    {
        if (!value.is_object())
        {
            return false;
        }
        const nlohmann::json::const_iterator it = value.find(k_RefKey);
        // 負の数は id として不正なので、負でない整数だけを受ける
        if (it == value.end() || !it->is_number_unsigned())
        {
            return false;
        }
        out = ActorRef{it->get<uint32>()};
        return true;
    }

    nlohmann::json* FindRefTarget(nlohmann::json& value) noexcept
    {
        if (!value.is_object())
        {
            return nullptr;
        }
        const nlohmann::json::iterator it = value.find(k_RefKey);
        if (it == value.end())
        {
            return nullptr;
        }
        return &*it;
    }

    void RemapActorRefs(nlohmann::json& actorJson, const std::unordered_map<uint32, uint32>& idMap)
    {
        ForEachRefValue(actorJson, [&idMap](nlohmann::json& target) {
            if (!target.is_number_unsigned())
            {
                return;
            }
            const std::unordered_map<uint32, uint32>::const_iterator mapped = idMap.find(target.get<uint32>());
            if (mapped != idMap.end())
            {
                target = mapped->second;
            }
        });
    }
} // namespace NS
