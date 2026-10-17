#include "NSlib/CoreObject/ClassObjs/Transform.h"

#include <algorithm>

namespace NS
{

    void Transform::SetPosition(const Vector3& position) noexcept
    {
        m_position = position;
    }

    void Transform::SetRotation(const Quaternion& rotation) noexcept
    {
        m_rotation = rotation;
    }

    void Transform::SetScale(const Vector3& scale) noexcept
    {
        m_scale = scale;
    }

    void Transform::SetScaleField(const Vector3& scale) noexcept
    {
        constexpr float k_MinScale = 0.01f;
        SetScale(
            Vector3{std::max(scale.x, k_MinScale), std::max(scale.y, k_MinScale), std::max(scale.z, k_MinScale)});
    }

    void Transform::ShiftPosition(const Vector3& delta) noexcept
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

    Matrix Transform::WorldMatrix() const noexcept
    {
        const Matrix scale = Matrix::CreateScale(m_scale);
        const Matrix rotate = Matrix::CreateFromQuaternion(m_rotation);
        const Matrix translate = Matrix::CreateTranslation(m_position);
        return scale * rotate * translate;
    }

    Matrix Transform::InterpolatedWorldMatrix(float alpha) const noexcept
    {
        // 前 + (今 − 前) は丸めで今からずれる
        // 止めた世界を描く割合 1 の絵が、前の値の違いで変わらないようにする
        if (alpha >= 1.0f)
        {
            return WorldMatrix();
        }
        const Vector3 position = Vector3::Lerp(m_previousPosition, m_position, alpha);
        const Quaternion rotation = Quaternion::Slerp(m_previousRotation, m_rotation, alpha);
        const Vector3 scale = Vector3::Lerp(m_previousScale, m_scale, alpha);

        const Matrix scaleM = Matrix::CreateScale(scale);
        const Matrix rotateM = Matrix::CreateFromQuaternion(rotation);
        const Matrix translateM = Matrix::CreateTranslation(position);
        return scaleM * rotateM * translateM;
    }

} // namespace NS
