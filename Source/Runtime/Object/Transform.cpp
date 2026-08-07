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

    void Transform::SetPosition(const NS::Core::Vector3& position) noexcept
    {
        m_position = position;
    }

    void Transform::SetRotation(const NS::Core::Quaternion& rotation) noexcept
    {
        m_rotation = rotation;
    }

    void Transform::SetScale(const NS::Core::Vector3& scale) noexcept
    {
        m_scale = scale;
    }

    void Transform::Snapshot() noexcept
    {
        m_previousPosition = m_position;
        m_previousRotation = m_rotation;
        m_previousScale = m_scale;
    }

    NS::Core::Matrix Transform::LocalMatrix() const noexcept
    {
        const NS::Core::Matrix scale = NS::Core::Matrix::CreateScale(m_scale);
        const NS::Core::Matrix rotate = NS::Core::Matrix::CreateFromQuaternion(m_rotation);
        const NS::Core::Matrix translate = NS::Core::Matrix::CreateTranslation(m_position);
        return scale * rotate * translate;
    }

    NS::Core::Matrix Transform::WorldMatrix() const noexcept
    {
        if (m_parent == nullptr)
            return LocalMatrix();
        return LocalMatrix() * m_parent->WorldMatrix();
    }

    NS::Core::Matrix Transform::InterpolatedLocalMatrix(float alpha) const noexcept
    {
        const NS::Core::Vector3 position = NS::Core::Vector3::Lerp(m_previousPosition, m_position, alpha);
        const NS::Core::Quaternion rotation = NS::Core::Quaternion::Slerp(m_previousRotation, m_rotation, alpha);
        const NS::Core::Vector3 scale = NS::Core::Vector3::Lerp(m_previousScale, m_scale, alpha);

        const NS::Core::Matrix scaleM = NS::Core::Matrix::CreateScale(scale);
        const NS::Core::Matrix rotateM = NS::Core::Matrix::CreateFromQuaternion(rotation);
        const NS::Core::Matrix translateM = NS::Core::Matrix::CreateTranslation(position);
        return scaleM * rotateM * translateM;
    }

    NS::Core::Matrix Transform::InterpolatedWorldMatrix(float alpha) const noexcept
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
