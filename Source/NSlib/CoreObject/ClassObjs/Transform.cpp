#include "NSlib/CoreObject/ClassObjs/Transform.h"

#include <algorithm>

namespace NS
{

    void Transform::SetPosition(const Vector3& position) noexcept { m_position.Set(position); }

    void Transform::SetRotation(const Quaternion& rotation) noexcept { m_rotation.Set(rotation); }

    void Transform::SetScale(const Vector3& scale) noexcept { m_scale.Set(scale); }

    void Transform::SetScaleField(const Vector3& scale) noexcept
    {
        constexpr float k_MinScale = 0.01f;
        SetScale(
            Vector3{std::max(scale.x, k_MinScale), std::max(scale.y, k_MinScale), std::max(scale.z, k_MinScale)});
    }

    void Transform::ShiftPosition(const Vector3& delta) noexcept { m_position.Shift(delta); }

    void Transform::Teleport(const Vector3& position) noexcept { m_position.Teleport(position); }

    void Transform::Snapshot() noexcept
    {
        m_position.Snapshot();
        m_rotation.Snapshot();
        m_scale.Snapshot();
    }

    Matrix Transform::WorldMatrix() const noexcept { return InterpolatedWorldMatrix(1.0f); }

    Matrix Transform::InterpolatedWorldMatrix(float alpha) const noexcept
    {
        const Matrix scale = Matrix::CreateScale(m_scale.Lerp(alpha));
        const Matrix rotate = Matrix::CreateFromQuaternion(m_rotation.Lerp(alpha));
        const Matrix translate = Matrix::CreateTranslation(m_position.Lerp(alpha));
        return scale * rotate * translate;
    }

} // namespace NS
