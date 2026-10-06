#include "NSlib/Object/Reflection/ComponentEntry.h"

#include "NSlib/Object/ObjectJson.h"

namespace NS::Obj
{
    namespace
    {
        // fields から name 一致の値を返す。値を読む Field 関数と HasField の共通処理
        const nlohmann::json* FindFieldValue(const nlohmann::json& entry, std::string_view name) noexcept
        {
            const nlohmann::json* fields = ComponentEntryFields(entry);
            if (fields == nullptr)
            {
                return nullptr;
            }
            for (nlohmann::json::const_iterator it = fields->begin(); it != fields->end(); ++it)
            {
                if (it.key() == name)
                {
                    return &it.value();
                }
            }
            return nullptr;
        }

        // entry の fields object を返し、無ければ作る。SetField の書き込み先
        nlohmann::json& EnsureFields(nlohmann::json& entry)
        {
            if (!entry.is_object())
            {
                entry = nlohmann::json::object();
            }
            if (!entry.contains("type"))
            {
                return entry;
            }
            nlohmann::json& fields = entry["fields"];
            if (!fields.is_object())
            {
                fields = nlohmann::json::object();
            }
            return fields;
        }
    } // namespace

    const nlohmann::json* ComponentEntryFields(const nlohmann::json& entry) noexcept
    {
        if (!entry.is_object())
        {
            return nullptr;
        }
        if (!entry.contains("type"))
        {
            return &entry;
        }
        const nlohmann::json::const_iterator it = entry.find("fields");
        if (it == entry.end() || !it->is_object())
        {
            return nullptr;
        }
        return &*it;
    }

    float FieldFloat(const nlohmann::json& entry, std::string_view name, float fallback) noexcept
    {
        const nlohmann::json* value = FindFieldValue(entry, name);
        if (value == nullptr || !value->is_number())
        {
            return fallback;
        }
        return value->get<float>();
    }

    int FieldInt(const nlohmann::json& entry, std::string_view name, int fallback) noexcept
    {
        const nlohmann::json* value = FindFieldValue(entry, name);
        if (value == nullptr || !value->is_number())
        {
            return fallback;
        }
        return value->get<int>();
    }

    bool ReadVector3(const nlohmann::json& value, NS::Vector3& out) noexcept
    {
        if (!value.is_array() || value.size() != 3u)
        {
            return false;
        }
        if (!value[0].is_number() || !value[1].is_number() || !value[2].is_number())
        {
            return false;
        }
        out = NS::Vector3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        return true;
    }

    bool ReadQuaternion(const nlohmann::json& value, NS::Quaternion& out) noexcept
    {
        if (!value.is_array() || value.size() != 4u)
        {
            return false;
        }
        if (!value[0].is_number() || !value[1].is_number() || !value[2].is_number() || !value[3].is_number())
        {
            return false;
        }
        out =
            NS::Quaternion{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
        return true;
    }

    NS::Vector3 FieldVector3(const nlohmann::json& entry, std::string_view name, const NS::Vector3& fallback) noexcept
    {
        const nlohmann::json* value = FindFieldValue(entry, name);
        NS::Vector3 out = fallback;
        if (value != nullptr)
        {
            (void)ReadVector3(*value, out);
        }
        return out;
    }

    NS::Quaternion FieldQuaternion(const nlohmann::json& entry,
                                   std::string_view name,
                                   const NS::Quaternion& fallback) noexcept
    {
        const nlohmann::json* value = FindFieldValue(entry, name);
        NS::Quaternion out = fallback;
        if (value != nullptr)
        {
            (void)ReadQuaternion(*value, out);
        }
        return out;
    }

    std::string FieldString(const nlohmann::json& entry, std::string_view name, std::string_view fallback)
    {
        const nlohmann::json* value = FindFieldValue(entry, name);
        if (value == nullptr || !value->is_string())
        {
            return std::string(fallback);
        }
        return value->get<std::string>();
    }

    ActorRef FieldObjectRef(const nlohmann::json& entry, std::string_view name) noexcept
    {
        const nlohmann::json* value = FindFieldValue(entry, name);
        if (value == nullptr || !value->is_object())
        {
            return ActorRef{};
        }
        const nlohmann::json::const_iterator refIt = value->find("ref");
        if (refIt == value->end() || !refIt->is_number_unsigned())
        {
            return ActorRef{};
        }
        return ActorRef{refIt->get<std::uint32_t>()};
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

    void SetField(nlohmann::json& entry, std::string_view name, const NS::Vector3& value)
    {
        EnsureFields(entry)[std::string(name)] = nlohmann::json{value.x, value.y, value.z};
    }

    void SetField(nlohmann::json& entry, std::string_view name, const NS::Quaternion& value)
    {
        EnsureFields(entry)[std::string(name)] = nlohmann::json{value.x, value.y, value.z, value.w};
    }

    void SetField(nlohmann::json& entry, std::string_view name, std::string_view value)
    {
        EnsureFields(entry)[std::string(name)] = std::string(value);
    }

    void SetField(nlohmann::json& entry, std::string_view name, const char* value)
    {
        SetField(entry, name, std::string_view{value});
    }

    void SetField(nlohmann::json& entry, std::string_view name, ActorRef value)
    {
        // 素の数値だと読込時に Int と区別できないため {"ref": id} の単キー object で書く
        nlohmann::json ref;
        ref["ref"] = value.id;
        EnsureFields(entry)[std::string(name)] = std::move(ref);
    }

} // namespace NS::Obj
