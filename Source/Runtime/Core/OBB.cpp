#include "Runtime/Core/OBB.h"

#include <cmath>

namespace NS::Core
{
    OBB MakeOBB(const Vector3& center, const Quaternion& rotation, const Vector3& halfExtents) noexcept
    {
        OBB obb;
        obb.center = center;
        obb.axisX = Vector3::Transform(Vector3::UnitX, rotation);
        obb.axisY = Vector3::Transform(Vector3::UnitY, rotation);
        obb.axisZ = Vector3::Transform(Vector3::UnitZ, rotation);
        obb.halfExtentX = std::abs(halfExtents.x);
        obb.halfExtentY = std::abs(halfExtents.y);
        obb.halfExtentZ = std::abs(halfExtents.z);
        return obb;
    }
} // namespace NS::Core
