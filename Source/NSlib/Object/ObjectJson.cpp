#include "NSlib/Object/ObjectJson.h"

#include "NSlib/Object/Reflection/ComponentEntry.h"

#include <string>

namespace NS::Obj
{
    const nlohmann::json& ObjectJsonParts(const nlohmann::json& object) noexcept
    {
        static const nlohmann::json empty = nlohmann::json::object();
        if (!object.is_object())
        {
            return empty;
        }
        const nlohmann::json::const_iterator found = object.find("parts");
        if (found == object.end() || !found->is_object())
        {
            return empty;
        }
        return *found;
    }

    nlohmann::json& ObjectJsonParts(nlohmann::json& object)
    {
        if (!object.is_object())
        {
            object = nlohmann::json::object();
        }
        nlohmann::json& parts = object["parts"];
        if (!parts.is_object())
        {
            parts = nlohmann::json::object();
        }
        return parts;
    }

    const nlohmann::json* PartFields(const nlohmann::json& object, std::string_view partName) noexcept
    {
        const nlohmann::json& parts = ObjectJsonParts(object);
        const nlohmann::json::const_iterator found = parts.find(std::string{partName});
        if (found == parts.end() || !found->is_object())
        {
            return nullptr;
        }
        return &*found;
    }

    nlohmann::json* PartFields(nlohmann::json& object, std::string_view partName) noexcept
    {
        const nlohmann::json* found = PartFields(static_cast<const nlohmann::json&>(object), partName);
        if (found == nullptr)
        {
            return nullptr;
        }
        return &ObjectJsonParts(object)[std::string{partName}];
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
        object["parts"] = nlohmann::json::object();
        return object;
    }

    std::uint32_t ObjectJsonId(const nlohmann::json& object) noexcept
    {
        return FieldUnsigned(object, "id", 0);
    }

    void SetObjectJsonId(nlohmann::json& object, std::uint32_t id)
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

    std::uint32_t ObjectJsonParent(const nlohmann::json& object) noexcept
    {
        return FieldUnsigned(object, "parent", k_NoObjectId);
    }

    void SetObjectJsonParent(nlohmann::json& object, std::uint32_t parentId)
    {
        WriteOrErase(object, "parent", parentId == k_NoObjectId, parentId);
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

    void RemapObjectRefs(nlohmann::json& object, const std::unordered_map<std::uint32_t, std::uint32_t>& idMap)
    {
        ForEachRefValue(object, [&idMap](nlohmann::json& value) {
            const nlohmann::json::iterator it = value.find("ref");
            if (it == value.end() || !it->is_number_unsigned())
            {
                return;
            }
            const std::unordered_map<std::uint32_t, std::uint32_t>::const_iterator mapped =
                idMap.find(it->get<std::uint32_t>());
            if (mapped != idMap.end())
            {
                *it = mapped->second;
            }
        });
    }
} // namespace NS::Obj
