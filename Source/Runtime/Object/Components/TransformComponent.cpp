#include "Runtime/Object/Components/TransformComponent.h"

#include "Runtime/Object/Reflection/ComponentEntry.h"

#include <algorithm>

namespace NS::Object
{
    void TransformComponent::SetPosition(const NS::Math::Vector3& position) noexcept
    {
        m_transform.SetPosition(position);
    }

    NS::Math::Vector3 TransformComponent::Position() const noexcept
    {
        return m_transform.Position();
    }

    void TransformComponent::SetRotationEulerDegrees(const NS::Math::Vector3& eulerDegrees) noexcept
    {
        m_transform.SetRotation(NS::Math::EulerDegreesToQuaternion(eulerDegrees));
    }

    NS::Math::Vector3 TransformComponent::RotationEulerDegrees() const noexcept
    {
        return NS::Math::QuaternionToEulerDegrees(m_transform.Rotation());
    }

    void TransformComponent::SetScale(const NS::Math::Vector3& scale) noexcept
    {
        // 0 / 負になると描画と当たり判定が壊れるため最小正値で止める
        constexpr float k_MinScale = 0.01f;
        m_transform.SetScale(NS::Math::Vector3{
            std::max(scale.x, k_MinScale), std::max(scale.y, k_MinScale), std::max(scale.z, k_MinScale)});
    }

    NS::Math::Vector3 TransformComponent::Scale() const noexcept
    {
        return m_transform.Scale();
    }

    namespace
    {
        constexpr std::string_view k_PositionFieldName = "位置";
        constexpr std::string_view k_RotationFieldName = "回転 (度)";
        constexpr std::string_view k_ScaleFieldName = "スケール";

        NS::Math::Vector3 ReadTransformVec3(const ObjectData& object,
                                            std::string_view fieldName,
                                            const NS::Math::Vector3& fallback) noexcept
        {
            const nlohmann::json* transform = FindComponentEntry(object, k_TransformTypeName);
            if (transform == nullptr)
                return fallback;
            return FieldVector3(*transform, fieldName, fallback);
        }

        void WriteTransformVec3(ObjectData& object, std::string_view fieldName, const NS::Math::Vector3& value)
        {
            SetField(EnsureTransformComponent(object), fieldName, value);
        }
    } // namespace

    nlohmann::json& EnsureTransformComponent(ObjectData& object)
    {
        if (!object.components.is_array())
            object.components = nlohmann::json::array();
        for (nlohmann::json& entry : object.components)
        {
            if (ComponentEntryType(entry) == k_TransformTypeName)
                return entry;
        }
        nlohmann::json transform = MakeComponentEntry(k_TransformTypeName);
        SetField(transform, k_PositionFieldName, NS::Math::Vector3{0.0f, 0.0f, 0.0f});
        SetField(transform, k_RotationFieldName, NS::Math::Vector3{0.0f, 0.0f, 0.0f});
        SetField(transform, k_ScaleFieldName, NS::Math::Vector3{1.0f, 1.0f, 1.0f});
        object.components.push_back(std::move(transform));
        return object.components.back();
    }

    NS::Math::Vector3 ObjectPosition(const ObjectData& object) noexcept
    {
        return ReadTransformVec3(object, k_PositionFieldName, NS::Math::Vector3{0.0f, 0.0f, 0.0f});
    }

    void SetObjectPosition(ObjectData& object, const NS::Math::Vector3& position) noexcept
    {
        WriteTransformVec3(object, k_PositionFieldName, position);
    }

    NS::Math::Quaternion ObjectRotation(const ObjectData& object) noexcept
    {
        const NS::Math::Vector3 euler =
            ReadTransformVec3(object, k_RotationFieldName, NS::Math::Vector3{0.0f, 0.0f, 0.0f});
        return NS::Math::EulerDegreesToQuaternion(euler);
    }

    void SetObjectRotation(ObjectData& object, const NS::Math::Quaternion& rotation) noexcept
    {
        WriteTransformVec3(object, k_RotationFieldName, NS::Math::QuaternionToEulerDegrees(rotation));

        // 厳密回転の控えが載っている間は組み立てでそちらが勝つので、置き去りにすると回転が戻る
        nlohmann::json* transform = FindComponentEntry(object, k_TransformTypeName);
        if (transform == nullptr)
            return;
        const auto fieldsIt = transform->find("fields");
        if (fieldsIt == transform->end() || !fieldsIt->is_object())
            return;
        const auto quatIt = fieldsIt->find(std::string(k_RotationQuatFieldName));
        if (quatIt != fieldsIt->end())
            *quatIt = nlohmann::json{rotation.x, rotation.y, rotation.z, rotation.w};
    }

    NS::Math::Vector3 ObjectScale(const ObjectData& object) noexcept
    {
        return ReadTransformVec3(object, k_ScaleFieldName, NS::Math::Vector3{1.0f, 1.0f, 1.0f});
    }

    void SetObjectScale(ObjectData& object, const NS::Math::Vector3& scale) noexcept
    {
        WriteTransformVec3(object, k_ScaleFieldName, scale);
    }
} // namespace NS::Object
