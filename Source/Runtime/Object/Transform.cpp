#include "Runtime/Object/Transform.h"

#include <algorithm>

namespace NS::Object
{

    Transform::~Transform() noexcept
    {
        DetachFromParent();
        for (Transform* child : m_children)
        {
            if (child != nullptr)
                child->m_parent = nullptr;
        }
    }

    void Transform::SetPosition(const NS::Math::Vector3& position) noexcept
    {
        m_position = position;
    }

    void Transform::SetRotation(const NS::Math::Quaternion& rotation) noexcept
    {
        m_rotation = rotation;
    }

    void Transform::SetScale(const NS::Math::Vector3& scale) noexcept
    {
        m_scale = scale;
    }

    void Transform::Snapshot() noexcept
    {
        m_previousPosition = m_position;
        m_previousRotation = m_rotation;
        m_previousScale = m_scale;
    }

    NS::Math::Matrix Transform::LocalMatrix() const noexcept
    {
        const NS::Math::Matrix scale = NS::Math::Matrix::CreateScale(m_scale);
        const NS::Math::Matrix rotate = NS::Math::Matrix::CreateFromQuaternion(m_rotation);
        const NS::Math::Matrix translate = NS::Math::Matrix::CreateTranslation(m_position);
        return scale * rotate * translate;
    }

    NS::Math::Matrix Transform::WorldMatrix() const noexcept
    {
        if (m_parent == nullptr)
            return LocalMatrix();
        return LocalMatrix() * m_parent->WorldMatrix();
    }

    NS::Math::Matrix Transform::InterpolatedLocalMatrix(float alpha) const noexcept
    {
        const NS::Math::Vector3 position = NS::Math::Vector3::Lerp(m_previousPosition, m_position, alpha);
        const NS::Math::Quaternion rotation = NS::Math::Quaternion::Slerp(m_previousRotation, m_rotation, alpha);
        const NS::Math::Vector3 scale = NS::Math::Vector3::Lerp(m_previousScale, m_scale, alpha);

        const NS::Math::Matrix scaleM = NS::Math::Matrix::CreateScale(scale);
        const NS::Math::Matrix rotateM = NS::Math::Matrix::CreateFromQuaternion(rotation);
        const NS::Math::Matrix translateM = NS::Math::Matrix::CreateTranslation(position);
        return scaleM * rotateM * translateM;
    }

    NS::Math::Matrix Transform::InterpolatedWorldMatrix(float alpha) const noexcept
    {
        if (m_parent == nullptr)
            return InterpolatedLocalMatrix(alpha);
        return InterpolatedLocalMatrix(alpha) * m_parent->InterpolatedWorldMatrix(alpha);
    }

    void Transform::SetParent(Transform* parent) noexcept
    {
        if (parent == m_parent)
            return;
        DetachFromParent();
        m_parent = parent;
        if (m_parent != nullptr)
            m_parent->m_children.push_back(this);
    }

    void Transform::DetachFromParent() noexcept
    {
        if (m_parent == nullptr)
            return;
        auto& siblings = m_parent->m_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
        m_parent = nullptr;
    }

} // namespace NS::Object
