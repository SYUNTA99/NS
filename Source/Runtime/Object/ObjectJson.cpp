#include "Runtime/Object/ObjectJson.h"

#include <string>

namespace NS::Obj
{
    namespace
    {
        // object の key が unsigned の数値なら返す。無いか壊れていれば fallback
        [[nodiscard]] std::uint32_t ReadUnsigned(const nlohmann::json& object, const char* key, std::uint32_t fallback) noexcept
        {
            if (!object.is_object())
            {
                return fallback;
            }
            const nlohmann::json::const_iterator it = object.find(key);
            if (it == object.end() || !it->is_number_unsigned())
            {
                return fallback;
            }
            return it->get<std::uint32_t>();
        }

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

    nlohmann::json MakeObjectJson(nlohmann::json components)
    {
        nlohmann::json object = nlohmann::json::object();
        object["id"] = 0u;
        if (!components.is_array())
        {
            components = nlohmann::json::array();
        }
        object["components"] = std::move(components);
        return object;
    }

    std::uint32_t ObjectJsonId(const nlohmann::json& object) noexcept
    {
        return ReadUnsigned(object, "id", 0);
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
        return ReadUnsigned(object, "parent", k_NoObjectId);
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

    const nlohmann::json& ObjectJsonComponents(const nlohmann::json& object) noexcept
    {
        static const nlohmann::json k_Empty = nlohmann::json::array();
        if (!object.is_object())
        {
            return k_Empty;
        }
        const nlohmann::json::const_iterator it = object.find("components");
        if (it == object.end() || !it->is_array())
        {
            return k_Empty;
        }
        return *it;
    }

    nlohmann::json& ObjectJsonComponents(nlohmann::json& object)
    {
        if (!object.is_object())
        {
            object = nlohmann::json::object();
        }
        nlohmann::json& components = object["components"];
        if (!components.is_array())
        {
            components = nlohmann::json::array();
        }
        return components;
    }

    void RemapObjectRefs(nlohmann::json& object, const std::unordered_map<std::uint32_t, std::uint32_t>& idMap)
    {
        ForEachRefValue(object, [&idMap](nlohmann::json& value) {
            for (const char* key : {"ref", "component"})
            {
                const nlohmann::json::iterator it = value.find(key);
                if (it == value.end() || !it->is_number_unsigned())
                {
                    continue;
                }
                const std::unordered_map<std::uint32_t, std::uint32_t>::const_iterator mapped =
                    idMap.find(it->get<std::uint32_t>());
                if (mapped != idMap.end())
                {
                    *it = mapped->second;
                }
            }
        });
    }
} // namespace NS::Obj
