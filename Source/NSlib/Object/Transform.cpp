#include "NSlib/Object/Transform.h"

#include <algorithm>

namespace NS::Obj
{

    Transform::~Transform() noexcept
    {
        DetachFromParent();
        for (Transform* child : m_children)
        {
            if (child != nullptr)
            {
				child->m_parent = nullptr;
            }
        }
    }

    void Transform::SetPosition(const NS::Vector3& position) noexcept
    {
        m_position = position;
    }

    void Transform::SetRotation(const NS::Quaternion& rotation) noexcept
    {
        m_rotation = rotation;
    }

    void Transform::SetScale(const NS::Vector3& scale) noexcept
    {
        m_scale = scale;
    }

    void Transform::ShiftPosition(const NS::Vector3& delta) noexcept
    {
        m_position += delta;
        m_previousPosition += delta;
    }

    void Transform::Snapshot() noexcept
    {
        m_previousPosition = m_position;
        m_previousRotation = m_rotation;
        m_previousScale = m_scale;
    }

    NS::Matrix Transform::LocalMatrix() const noexcept
    {
        const NS::Matrix scale = NS::Matrix::CreateScale(m_scale);
        const NS::Matrix rotate = NS::Matrix::CreateFromQuaternion(m_rotation);
        const NS::Matrix translate = NS::Matrix::CreateTranslation(m_position);
        return scale * rotate * translate;
    }

    NS::Matrix Transform::WorldMatrix() const noexcept
    {
        if (m_parent == nullptr)
        {
			return LocalMatrix();
        }
        return LocalMatrix() * m_parent->WorldMatrix();
    }

    NS::Matrix Transform::InterpolatedLocalMatrix(float alpha) const noexcept
    {
        // 前 + (今 − 前) は丸めで今からずれる
        // 止めた世界を描く割合 1 の絵が、前の値の違いで変わらないようにする
        if (alpha >= 1.0f)
        {
            return LocalMatrix();
        }
        const NS::Vector3 position = NS::Vector3::Lerp(m_previousPosition, m_position, alpha);
        const NS::Quaternion rotation = NS::Quaternion::Slerp(m_previousRotation, m_rotation, alpha);
        const NS::Vector3 scale = NS::Vector3::Lerp(m_previousScale, m_scale, alpha);

        const NS::Matrix scaleM = NS::Matrix::CreateScale(scale);
        const NS::Matrix rotateM = NS::Matrix::CreateFromQuaternion(rotation);
        const NS::Matrix translateM = NS::Matrix::CreateTranslation(position);
        return scaleM * rotateM * translateM;
    }

    NS::Matrix Transform::InterpolatedWorldMatrix(float alpha) const noexcept
    {
        if (m_parent == nullptr)
        {
            return InterpolatedLocalMatrix(alpha);
        }
        return InterpolatedLocalMatrix(alpha) * m_parent->InterpolatedWorldMatrix(alpha);
    }

    void Transform::SetParent(Transform* parent) noexcept
    {
        if (parent == m_parent)
        {
            return;
        }
        DetachFromParent();
        m_parent = parent;
        if (m_parent != nullptr)
        {
            m_parent->m_children.push_back(this);
        }
    }

    void Transform::DetachFromParent() noexcept
    {
        if (m_parent == nullptr)
        {
            return;
        }
        std::vector<Transform*>& siblings = m_parent->m_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
        m_parent = nullptr;
    }

} // namespace NS::Obj
