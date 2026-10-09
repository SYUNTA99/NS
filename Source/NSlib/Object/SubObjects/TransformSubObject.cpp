#include "NSlib/Object/SubObjects/TransformSubObject.h"

#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Reflection/SubObjectEntry.h"

#include <algorithm>
#include <string>

namespace NS::Obj
{
    void TransformSubObject::SetPosition(const NS::Vector3& position) noexcept
    {
        m_transform.SetPosition(position);
    }

    NS::Vector3 TransformSubObject::Position() const noexcept
    {
        return m_transform.Position();
    }

    void TransformSubObject::SetRotation(const NS::Quaternion& rotation) noexcept
    {
        m_transform.SetRotation(rotation);
    }

    NS::Quaternion TransformSubObject::Rotation() const noexcept
    {
        return m_transform.Rotation();
    }

    void TransformSubObject::SetScale(const NS::Vector3& scale) noexcept
    {
        // 0 / 負になると描画と当たり判定が壊れるため最小正値で止める
        constexpr float k_MinScale = 0.01f;
        m_transform.SetScale(NS::Vector3{
            std::max(scale.x, k_MinScale), std::max(scale.y, k_MinScale), std::max(scale.z, k_MinScale)});
    }

    NS::Vector3 TransformSubObject::Scale() const noexcept
    {
        return m_transform.Scale();
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
