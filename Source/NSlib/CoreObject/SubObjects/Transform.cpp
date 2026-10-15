#include "NSlib/CoreObject/SubObjects/Transform.h"

#include <algorithm>

namespace NS
{

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

    void Transform::SetScaleField(const NS::Vector3& scale) noexcept
    {
        constexpr float k_MinScale = 0.01f;
        SetScale(
            NS::Vector3{std::max(scale.x, k_MinScale), std::max(scale.y, k_MinScale), std::max(scale.z, k_MinScale)});
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

    NS::Matrix Transform::WorldMatrix() const noexcept
    {
        const NS::Matrix scale = NS::Matrix::CreateScale(m_scale);
        const NS::Matrix rotate = NS::Matrix::CreateFromQuaternion(m_rotation);
        const NS::Matrix translate = NS::Matrix::CreateTranslation(m_position);
        return scale * rotate * translate;
    }

    NS::Matrix Transform::InterpolatedWorldMatrix(float alpha) const noexcept
    {
        // 前 + (今 − 前) は丸めで今からずれる
        // 止めた世界を描く割合 1 の絵が、前の値の違いで変わらないようにする
        if (alpha >= 1.0f)
        {
            return WorldMatrix();
        }
        const NS::Vector3 position = NS::Vector3::Lerp(m_previousPosition, m_position, alpha);
        const NS::Quaternion rotation = NS::Quaternion::Slerp(m_previousRotation, m_rotation, alpha);
        const NS::Vector3 scale = NS::Vector3::Lerp(m_previousScale, m_scale, alpha);

        const NS::Matrix scaleM = NS::Matrix::CreateScale(scale);
        const NS::Matrix rotateM = NS::Matrix::CreateFromQuaternion(rotation);
        const NS::Matrix translateM = NS::Matrix::CreateTranslation(position);
        return scaleM * rotateM * translateM;
    }

} // namespace NS
