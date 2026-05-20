#include "ns/scene/transform.h"

#include <algorithm>

namespace ns::scene
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

    void Transform::SetPosition(const ns::core::Vector3& position) noexcept
    {
        m_position = position;
    }

    void Transform::SetRotation(const ns::core::Quaternion& rotation) noexcept
    {
        m_rotation = rotation;
    }

    void Transform::SetScale(const ns::core::Vector3& scale) noexcept
    {
        m_scale = scale;
    }

    void Transform::Snapshot() noexcept
    {
        m_previousPosition = m_position;
        m_previousRotation = m_rotation;
        m_previousScale = m_scale;
    }

    ns::core::Matrix Transform::LocalMatrix() const noexcept
    {
        const ns::core::Matrix scale = ns::core::Matrix::CreateScale(m_scale);
        const ns::core::Matrix rotate = ns::core::Matrix::CreateFromQuaternion(m_rotation);
        const ns::core::Matrix translate = ns::core::Matrix::CreateTranslation(m_position);
        return scale * rotate * translate;
    }

    ns::core::Matrix Transform::WorldMatrix() const noexcept
    {
        if (m_parent == nullptr)
            return LocalMatrix();
        return LocalMatrix() * m_parent->WorldMatrix();
    }

    ns::core::Matrix Transform::InterpolatedLocalMatrix(float alpha) const noexcept
    {
        const ns::core::Vector3 position = ns::core::Vector3::Lerp(m_previousPosition, m_position, alpha);
        const ns::core::Quaternion rotation = ns::core::Quaternion::Slerp(m_previousRotation, m_rotation, alpha);
        const ns::core::Vector3 scale = ns::core::Vector3::Lerp(m_previousScale, m_scale, alpha);

        const ns::core::Matrix scaleM = ns::core::Matrix::CreateScale(scale);
        const ns::core::Matrix rotateM = ns::core::Matrix::CreateFromQuaternion(rotation);
        const ns::core::Matrix translateM = ns::core::Matrix::CreateTranslation(position);
        return scaleM * rotateM * translateM;
    }

    ns::core::Matrix Transform::InterpolatedWorldMatrix(float alpha) const noexcept
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

} // namespace ns::scene
