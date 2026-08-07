#include "Runtime/Object/Reflection/ComponentEntry.h"

#include "Runtime/Object/Scene/SceneData.h"

namespace NS::Object
{
    namespace
    {
        /// fields から name 一致の値を返す。 キー照合は alloc 無しで string_view 比較する
        const nlohmann::json* FindFieldValue(const nlohmann::json& entry, std::string_view name) noexcept
        {
            const nlohmann::json* fields = ComponentEntryFields(entry);
            if (fields == nullptr)
                return nullptr;
            for (auto it = fields->begin(); it != fields->end(); ++it)
            {
                if (it.key() == name)
                    return &it.value();
            }
            return nullptr;
        }

        /// entry の fields object を返し、 無ければ作る。 SetField の書き込み先
        nlohmann::json& EnsureFields(nlohmann::json& entry)
        {
            if (!entry.is_object())
                entry = nlohmann::json::object();
            nlohmann::json& fields = entry["fields"];
            if (!fields.is_object())
                fields = nlohmann::json::object();
            return fields;
        }
    } // namespace

    nlohmann::json MakeComponentEntry(std::string_view typeName, nlohmann::json fields)
    {
        nlohmann::json entry;
        entry["type"] = std::string(typeName);
        if (!fields.is_object())
            fields = nlohmann::json::object();
        entry["fields"] = std::move(fields);
        return entry;
    }

    std::string_view ComponentEntryType(const nlohmann::json& entry) noexcept
    {
        if (!entry.is_object())
            return {};
        const auto it = entry.find("type");
        if (it == entry.end() || !it->is_string())
            return {};
        return it->get_ref<const std::string&>();
    }

    std::uint32_t ComponentEntryId(const nlohmann::json& entry) noexcept
    {
        if (!entry.is_object())
            return 0;
        const auto it = entry.find("id");
        if (it == entry.end() || !it->is_number_unsigned())
            return 0;
        return it->get<std::uint32_t>();
    }

    void SetComponentEntryId(nlohmann::json& entry, std::uint32_t id)
    {
        if (!entry.is_object())
            return;
        if (id == 0)
        {
            entry.erase("id");
            return;
        }
        entry["id"] = id;
    }

    bool ComponentEntryEnabled(const nlohmann::json& entry) noexcept
    {
        if (!entry.is_object())
            return true;
        const auto it = entry.find("enabled");
        if (it == entry.end() || !it->is_boolean())
            return true;
        return it->get<bool>();
    }

    void SetComponentEntryEnabled(nlohmann::json& entry, bool enabled)
    {
        if (!entry.is_object())
            return;
        if (enabled)
        {
            entry.erase("enabled");
            return;
        }
        entry["enabled"] = false;
    }

    const nlohmann::json* ComponentEntryFields(const nlohmann::json& entry) noexcept
    {
        if (!entry.is_object())
            return nullptr;
        const auto it = entry.find("fields");
        if (it == entry.end() || !it->is_object())
            return nullptr;
        return &*it;
    }

    const nlohmann::json* FindComponentEntry(const ObjectData& object, std::string_view typeName) noexcept
    {
        if (!object.components.is_array())
            return nullptr;
        for (const nlohmann::json& entry : object.components)
        {
            if (ComponentEntryType(entry) == typeName)
                return &entry;
        }
        return nullptr;
    }

    nlohmann::json* FindComponentEntry(ObjectData& object, std::string_view typeName) noexcept
    {
        if (!object.components.is_array())
            return nullptr;
        for (nlohmann::json& entry : object.components)
        {
            if (ComponentEntryType(entry) == typeName)
                return &entry;
        }
        return nullptr;
    }

    float FieldFloat(const nlohmann::json& entry, std::string_view name, float fallback) noexcept
    {
        const nlohmann::json* value = FindFieldValue(entry, name);
        if (value == nullptr || !value->is_number())
            return fallback;
        return value->get<float>();
    }

    int FieldInt(const nlohmann::json& entry, std::string_view name, int fallback) noexcept
    {
        const nlohmann::json* value = FindFieldValue(entry, name);
        if (value == nullptr || !value->is_number())
            return fallback;
        return value->get<int>();
    }

    NS::Core::Vector3 FieldVector3(const nlohmann::json& entry,
                                   std::string_view name,
                                   const NS::Core::Vector3& fallback) noexcept
    {
        const nlohmann::json* value = FindFieldValue(entry, name);
        if (value == nullptr || !value->is_array() || value->size() != 3u)
            return fallback;
        const nlohmann::json& x = (*value)[0];
        const nlohmann::json& y = (*value)[1];
        const nlohmann::json& z = (*value)[2];
        if (!x.is_number() || !y.is_number() || !z.is_number())
            return fallback;
        return NS::Core::Vector3{x.get<float>(), y.get<float>(), z.get<float>()};
    }

    std::string FieldString(const nlohmann::json& entry, std::string_view name, std::string_view fallback)
    {
        const nlohmann::json* value = FindFieldValue(entry, name);
        if (value == nullptr || !value->is_string())
            return std::string(fallback);
        return value->get<std::string>();
    }

    ObjectRef FieldObjectRef(const nlohmann::json& entry, std::string_view name) noexcept
    {
        const nlohmann::json* value = FindFieldValue(entry, name);
        if (value == nullptr || !value->is_object())
            return ObjectRef{};
        const auto refIt = value->find("ref");
        if (refIt == value->end() || !refIt->is_number_unsigned())
            return ObjectRef{};
        return ObjectRef{refIt->get<std::uint32_t>()};
    }

    bool HasField(const nlohmann::json& entry, std::string_view name) noexcept
    {
        return FindFieldValue(entry, name) != nullptr;
    }

    void SetField(nlohmann::json& entry, std::string_view name, float value)
    {
        EnsureFields(entry)[std::string(name)] = value;
    }

    void SetField(nlohmann::json& entry, std::string_view name, int value)
    {
        EnsureFields(entry)[std::string(name)] = value;
    }

    void SetField(nlohmann::json& entry, std::string_view name, bool value)
    {
        EnsureFields(entry)[std::string(name)] = value;
    }

    void SetField(nlohmann::json& entry, std::string_view name, const NS::Core::Vector3& value)
    {
        EnsureFields(entry)[std::string(name)] = nlohmann::json{value.x, value.y, value.z};
    }

    void SetField(nlohmann::json& entry, std::string_view name, std::string_view value)
    {
        EnsureFields(entry)[std::string(name)] = std::string(value);
    }

    void SetField(nlohmann::json& entry, std::string_view name, const char* value)
    {
        SetField(entry, name, std::string_view{value});
    }

    void SetField(nlohmann::json& entry, std::string_view name, ObjectRef value)
    {
        // 素の数値だと読込時に Int と区別できないため {"ref": id} の単キー object で書く
        nlohmann::json ref;
        ref["ref"] = value.id;
        EnsureFields(entry)[std::string(name)] = std::move(ref);
    }

} // namespace NS::Object
