#include "Runtime/Object/Components/TransformComponent.h"

#include "Runtime/Object/ObjectJson.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"

#include <algorithm>

namespace NS::Obj
{
    void TransformComponent::SetPosition(const NS::Core::Vector3& position) noexcept
    {
        m_transform.SetPosition(position);
    }

    NS::Core::Vector3 TransformComponent::Position() const noexcept
    {
        return m_transform.Position();
    }

    void TransformComponent::SetRotation(const NS::Core::Quaternion& rotation) noexcept
    {
        m_transform.SetRotation(rotation);
    }

    NS::Core::Quaternion TransformComponent::Rotation() const noexcept
    {
        return m_transform.Rotation();
    }

    void TransformComponent::SetScale(const NS::Core::Vector3& scale) noexcept
    {
        // 0 / 負になると描画と当たり判定が壊れるため最小正値で止める
        constexpr float k_MinScale = 0.01f;
        m_transform.SetScale(NS::Core::Vector3{
            std::max(scale.x, k_MinScale), std::max(scale.y, k_MinScale), std::max(scale.z, k_MinScale)});
    }

    NS::Core::Vector3 TransformComponent::Scale() const noexcept
    {
        return m_transform.Scale();
    }

    namespace
    {
        NS::Core::Vector3 ReadTransformVec3(const nlohmann::json& object,
                                            std::string_view fieldName,
                                            const NS::Core::Vector3& fallback) noexcept
        {
            const nlohmann::json* transform = FindComponentEntry(object, k_TransformTypeName);
            if (transform == nullptr)
            {
                return fallback;
            }
            return FieldVector3(*transform, fieldName, fallback);
        }

        void WriteTransformVec3(nlohmann::json& object, std::string_view fieldName, const NS::Core::Vector3& value)
        {
            SetField(EnsureTransformComponent(object), fieldName, value);
        }
    } // namespace

    nlohmann::json& EnsureTransformComponent(nlohmann::json& object)
    {
        nlohmann::json& components = ObjectJsonComponents(object);
        for (nlohmann::json& entry : components)
        {
            if (ComponentEntryType(entry) == k_TransformTypeName)
            {
                return entry;
            }
        }
        nlohmann::json transform = MakeComponentEntry(k_TransformTypeName);
        SetField(transform, k_PositionFieldName, NS::Core::Vector3{0.0f, 0.0f, 0.0f});
        SetField(transform, k_RotationFieldName, NS::Core::Quaternion::Identity);
        SetField(transform, k_ScaleFieldName, NS::Core::Vector3{1.0f, 1.0f, 1.0f});
        components.push_back(std::move(transform));
        return components.back();
    }

    NS::Core::Vector3 ObjectPosition(const nlohmann::json& object) noexcept
    {
        return ReadTransformVec3(object, k_PositionFieldName, NS::Core::Vector3{0.0f, 0.0f, 0.0f});
    }

    void SetObjectPosition(nlohmann::json& object, const NS::Core::Vector3& position) noexcept
    {
        WriteTransformVec3(object, k_PositionFieldName, position);
    }

    NS::Core::Quaternion ObjectRotation(const nlohmann::json& object) noexcept
    {
        const nlohmann::json* transform = FindComponentEntry(object, k_TransformTypeName);
        if (transform == nullptr)
        {
            return NS::Core::Quaternion::Identity;
        }
        return FieldQuaternion(*transform, k_RotationFieldName, NS::Core::Quaternion::Identity);
    }

    void SetObjectRotation(nlohmann::json& object, const NS::Core::Quaternion& rotation) noexcept
    {
        SetField(EnsureTransformComponent(object), k_RotationFieldName, rotation);
    }

    NS::Core::Vector3 ObjectScale(const nlohmann::json& object) noexcept
    {
        return ReadTransformVec3(object, k_ScaleFieldName, NS::Core::Vector3{1.0f, 1.0f, 1.0f});
    }

    void SetObjectScale(nlohmann::json& object, const NS::Core::Vector3& scale) noexcept
    {
        WriteTransformVec3(object, k_ScaleFieldName, scale);
    }
} // namespace NS::Obj
