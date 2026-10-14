#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Object/Reflection/SubObjectEntry.h"
#include "NSlib/Object/SubObjects/Transform.h"

#include <string>

namespace NS::Obj
{
    const nlohmann::json& ObjectJsonSubObjs(const nlohmann::json& object) noexcept
    {
        static const nlohmann::json empty = nlohmann::json::object();
        if (!object.is_object())
        {
            return empty;
        }
        const nlohmann::json::const_iterator found = object.find("subObjects");
        if (found == object.end() || !found->is_object())
        {
            return empty;
        }
        return *found;
    }

    nlohmann::json& ObjectJsonSubObjs(nlohmann::json& object)
    {
        if (!object.is_object())
        {
            object = nlohmann::json::object();
        }
        nlohmann::json& parts = object["subObjects"];
        if (!parts.is_object())
        {
            parts = nlohmann::json::object();
        }
        return parts;
    }

    const nlohmann::json* SubObjFields(const nlohmann::json& object, std::string_view partName) noexcept
    {
        const nlohmann::json& parts = ObjectJsonSubObjs(object);
        const nlohmann::json::const_iterator found = parts.find(std::string{partName});
        if (found == parts.end() || !found->is_object())
        {
            return nullptr;
        }
        return &*found;
    }

    nlohmann::json* SubObjFields(nlohmann::json& object, std::string_view partName) noexcept
    {
        const nlohmann::json* found = SubObjFields(static_cast<const nlohmann::json&>(object), partName);
        if (found == nullptr)
        {
            return nullptr;
        }
        return &ObjectJsonSubObjs(object)[std::string{partName}];
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
        object["subObjects"] = nlohmann::json::object();
        return object;
    }

    NS::uint32 ObjectJsonId(const nlohmann::json& object) noexcept
    {
        return FieldUnsigned(object, "id", 0);
    }

    void SetObjectJsonId(nlohmann::json& object, NS::uint32 id)
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

    NS::uint32 ObjectJsonParent(const nlohmann::json& object) noexcept
    {
        return FieldUnsigned(object, "parent", k_NoObjectId);
    }

    void SetObjectJsonParent(nlohmann::json& object, NS::uint32 parentId)
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

    void RemapObjectRefs(nlohmann::json& object, const std::unordered_map<NS::uint32, NS::uint32>& idMap)
    {
        ForEachRefValue(object, [&idMap](nlohmann::json& value) {
            const nlohmann::json::iterator it = value.find("ref");
            if (it == value.end() || !it->is_number_unsigned())
            {
                return;
            }
            const std::unordered_map<NS::uint32, NS::uint32>::const_iterator mapped = idMap.find(it->get<NS::uint32>());
            if (mapped != idMap.end())
            {
                *it = mapped->second;
            }
        });
    }

    namespace
    {
        NS::Vector3 ReadTransformVec3(const nlohmann::json& object,
                                      std::string_view fieldName,
                                      const NS::Vector3& fallback) noexcept
        {
            const nlohmann::json* transform = SubObjFields(object, k_TransformSubObjName);
            if (transform == nullptr)
            {
                return fallback;
            }
            return FieldVector3(*transform, fieldName, fallback);
        }
    } // namespace

    nlohmann::json& EnsureTransformSubObject(nlohmann::json& object)
    {
        nlohmann::json& transform = ObjectJsonSubObjs(object)[std::string{k_TransformSubObjName}];
        if (!transform.is_object())
        {
            transform = nlohmann::json::object();
            SetField(transform, k_PositionFieldName, NS::Vector3{0.0f, 0.0f, 0.0f});
            SetField(transform, k_RotationFieldName, NS::Quaternion::Identity);
            SetField(transform, k_ScaleFieldName, NS::Vector3{1.0f, 1.0f, 1.0f});
        }
        return transform;
    }

    NS::Vector3 ObjectPosition(const nlohmann::json& object) noexcept
    {
        return ReadTransformVec3(object, k_PositionFieldName, NS::Vector3{0.0f, 0.0f, 0.0f});
    }

    void SetObjectPosition(nlohmann::json& object, const NS::Vector3& position) noexcept
    {
        SetField(EnsureTransformSubObject(object), k_PositionFieldName, position);
    }

    NS::Quaternion ObjectRotation(const nlohmann::json& object) noexcept
    {
        const nlohmann::json* transform = SubObjFields(object, k_TransformSubObjName);
        if (transform == nullptr)
        {
            return NS::Quaternion::Identity;
        }
        return FieldQuaternion(*transform, k_RotationFieldName, NS::Quaternion::Identity);
    }

    void SetObjectRotation(nlohmann::json& object, const NS::Quaternion& rotation) noexcept
    {
        SetField(EnsureTransformSubObject(object), k_RotationFieldName, rotation);
    }

    NS::Vector3 ObjectScale(const nlohmann::json& object) noexcept
    {
        return ReadTransformVec3(object, k_ScaleFieldName, NS::Vector3{1.0f, 1.0f, 1.0f});
    }

    void SetObjectScale(nlohmann::json& object, const NS::Vector3& scale) noexcept
    {
        SetField(EnsureTransformSubObject(object), k_ScaleFieldName, scale);
    }
} // namespace NS::Obj
