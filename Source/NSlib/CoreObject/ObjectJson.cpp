#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/Core/Types.h"

#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"

#include <string>

namespace NS
{
    const nlohmann::json& ObjectJsonClassObjs(const nlohmann::json& object) noexcept
    {
        static const nlohmann::json empty = nlohmann::json::object();
        if (!object.is_object())
        {
            return empty;
        }
        const nlohmann::json::const_iterator found = object.find("classObjs");
        if (found == object.end() || !found->is_object())
        {
            return empty;
        }
        return *found;
    }

    nlohmann::json& ObjectJsonClassObjs(nlohmann::json& object)
    {
        if (!object.is_object())
        {
            object = nlohmann::json::object();
        }
        nlohmann::json& parts = object["classObjs"];
        if (!parts.is_object())
        {
            parts = nlohmann::json::object();
        }
        return parts;
    }

    const nlohmann::json* ClassObjFields(const nlohmann::json& object, std::string_view partName) noexcept
    {
        const nlohmann::json& parts = ObjectJsonClassObjs(object);
        const nlohmann::json::const_iterator found = parts.find(std::string{partName});
        if (found == parts.end() || !found->is_object())
        {
            return nullptr;
        }
        return &*found;
    }

    nlohmann::json* ClassObjFields(nlohmann::json& object, std::string_view partName) noexcept
    {
        const nlohmann::json* found = ClassObjFields(static_cast<const nlohmann::json&>(object), partName);
        if (found == nullptr)
        {
            return nullptr;
        }
        return &ObjectJsonClassObjs(object)[std::string{partName}];
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

    nlohmann::json MakeObjectJson()
    {
        nlohmann::json object = nlohmann::json::object();
        object["id"] = 0u;
        object["classObjs"] = nlohmann::json::object();
        return object;
    }

    uint32 ObjectJsonId(const nlohmann::json& object) noexcept
    {
        return FieldUnsigned(object, "id", 0);
    }

    void SetObjectJsonId(nlohmann::json& object, uint32 id)
    {
        WriteOrErase(object, "id", false, id);
    }

    std::string_view ObjectJsonClass(const nlohmann::json& object) noexcept
    {
        return ReadString(object, "class");
    }

    void SetObjectJsonClass(nlohmann::json& object, std::string_view className)
    {
        WriteOrErase(object, "class", className.empty(), std::string{className});
    }

    std::string_view ObjectJsonName(const nlohmann::json& object) noexcept
    {
        return ReadString(object, "name");
    }

    void SetObjectJsonName(nlohmann::json& object, std::string_view name)
    {
        WriteOrErase(object, "name", name.empty(), std::string{name});
    }

    bool ObjectJsonActive(const nlohmann::json& object) noexcept
    {
        if (!object.is_object())
        {
            return true;
        }
        const nlohmann::json::const_iterator it = object.find("active");
        if (it == object.end() || !it->is_boolean())
        {
            return true;
        }
        return it->get<bool>();
    }

    void SetObjectJsonActive(nlohmann::json& object, bool active)
    {
        WriteOrErase(object, "active", active, active);
    }

    ObjectHeader ReadObjectHeader(const nlohmann::json& object)
    {
        return ObjectHeader{.id = ObjectJsonId(object),
                            .className = std::string{ObjectJsonClass(object)},
                            .name = std::string{ObjectJsonName(object)},
                            .active = ObjectJsonActive(object)};
    }

    void WriteObjectHeader(nlohmann::json& object, const ObjectHeader& header)
    {
        SetObjectJsonId(object, header.id);
        SetObjectJsonClass(object, header.className);
        SetObjectJsonName(object, header.name);
        SetObjectJsonActive(object, header.active);
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

    void RemapObjectRefs(nlohmann::json& object, const std::unordered_map<uint32, uint32>& idMap)
    {
        ForEachRefValue(object, [&idMap](nlohmann::json& target) {
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
