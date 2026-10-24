#include "NSlib/CoreObject/ClassObjs/DirectionalLight.h"

namespace NS
{
    Vector3 DirectionalLight::Direction(const Matrix& root) const noexcept
    {
        Vector3 direction = Vector3::TransformNormal(Vector3::UnitZ, root);
        direction.Normalize();
        return direction;
    }
} // namespace NS
