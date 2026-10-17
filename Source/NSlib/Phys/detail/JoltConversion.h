#pragma once

#include "NSlib/Core/Math.h"

#include <Jolt/Jolt.h>

#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

namespace NS
{
    namespace Phys
    {
        //! 軸と符号はそのまま変換する。三角形の巻き方は双方とも CCW
        [[nodiscard]] inline JPH::Vec3 ToJolt(const Vector3& value) noexcept
        {
            return JPH::Vec3{value.x, value.y, value.z};
        }

        [[nodiscard]] inline Vector3 FromJolt(JPH::Vec3Arg value) noexcept
        {
            return Vector3{value.GetX(), value.GetY(), value.GetZ()};
        }

        //! @brief NS::Quaternion を JPH::Quat へ変換する
        [[nodiscard]] inline JPH::Quat ToJolt(const Quaternion& value) noexcept
        {
            return JPH::Quat{value.x, value.y, value.z, value.w};
        }

        //! @brief JPH::Quat を NS::Quaternion へ変換する
        [[nodiscard]] inline Quaternion FromJolt(JPH::QuatArg value) noexcept
        {
            return Quaternion{value.GetX(), value.GetY(), value.GetZ(), value.GetW()};
        }

        [[nodiscard]] inline JPH::ShapeRefC ShapeOrNull(const JPH::ShapeSettings::ShapeResult& result)
        {
            if (result.HasError())
            {
                return nullptr;
            }
            return result.Get();
        }
    } // namespace Phys
} // namespace NS
