#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/Core/Types.h"

#include "NSlib/CoreObject/ClassObjs/Transform.h"
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

    void RemapObjectRefs(nlohmann::json& object, const std::unordered_map<uint32, uint32>& idMap)
    {
        ForEachRefValue(object, [&idMap](nlohmann::json& value) {
            const nlohmann::json::iterator it = value.find("ref");
            if (it == value.end() || !it->is_number_unsigned())
            {
                return;
            }
            const std::unordered_map<uint32, uint32>::const_iterator mapped = idMap.find(it->get<uint32>());
            if (mapped != idMap.end())
            {
                *it = mapped->second;
            }
        });
    }

    namespace
    {
        Vector3 ReadTransformVec3(const nlohmann::json& object,
                                      std::string_view fieldName,
                                      const Vector3& fallback) noexcept
        {
            const nlohmann::json* transform = ClassObjFields(object, k_TransformClassObjName);
            if (transform == nullptr)
            {
                return fallback;
            }
            return FieldVector3(*transform, fieldName, fallback);
        }
    } // namespace

    nlohmann::json& EnsureTransformClassObj(nlohmann::json& object)
    {
        nlohmann::json& transform = ObjectJsonClassObjs(object)[std::string{k_TransformClassObjName}];
        if (!transform.is_object())
        {
            transform = nlohmann::json::object();
            SetField(transform, k_PositionFieldName, Vector3{0.0f, 0.0f, 0.0f});
            SetField(transform, k_RotationFieldName, Quaternion::Identity);
            SetField(transform, k_ScaleFieldName, Vector3{1.0f, 1.0f, 1.0f});
        }
        return transform;
    }

    Vector3 ObjectPosition(const nlohmann::json& object) noexcept
    {
        return ReadTransformVec3(object, k_PositionFieldName, Vector3{0.0f, 0.0f, 0.0f});
    }

    void SetObjectPosition(nlohmann::json& object, const Vector3& position) noexcept
    {
        SetField(EnsureTransformClassObj(object), k_PositionFieldName, position);
    }

    Quaternion ObjectRotation(const nlohmann::json& object) noexcept
    {
        const nlohmann::json* transform = ClassObjFields(object, k_TransformClassObjName);
        if (transform == nullptr)
        {
            return Quaternion::Identity;
        }
        return FieldQuaternion(*transform, k_RotationFieldName, Quaternion::Identity);
    }

    void SetObjectRotation(nlohmann::json& object, const Quaternion& rotation) noexcept
    {
        SetField(EnsureTransformClassObj(object), k_RotationFieldName, rotation);
    }

    Vector3 ObjectScale(const nlohmann::json& object) noexcept
    {
        return ReadTransformVec3(object, k_ScaleFieldName, Vector3{1.0f, 1.0f, 1.0f});
    }

    void SetObjectScale(nlohmann::json& object, const Vector3& scale) noexcept
    {
        SetField(EnsureTransformClassObj(object), k_ScaleFieldName, scale);
    }
} // namespace NS
