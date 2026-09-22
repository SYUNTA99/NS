#pragma once

#include "Runtime/Core/Math.h"

#include <Jolt/Jolt.h>

#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Vec3.h>

namespace NS::Phys
{
    //! @brief NS::Core::Vector3 を JPH::Vec3 へ変換する
    //! @details 軸の入れ替えも符号反転もしない
    //! NS の Cross(v1 - v0, v2 - v0) と Jolt の (v3 - v2).Cross(v1 - v2) は展開すると同値、巻き方はどちらも CCW
    [[nodiscard]] inline JPH::Vec3 ToJolt(const NS::Core::Vector3& value) noexcept
    {
        return JPH::Vec3{value.x, value.y, value.z};
    }

    //! @brief JPH::Vec3 を NS::Core::Vector3 へ変換する
    //! @details 引数の型は Jolt が渡し方を決めている別名
    //! Vec3Arg の実体は const Vec3 の値渡しで、const JPH::Vec3& と直に書くと参照渡しになる
    [[nodiscard]] inline NS::Core::Vector3 FromJolt(JPH::Vec3Arg value) noexcept
    {
        return NS::Core::Vector3{value.GetX(), value.GetY(), value.GetZ()};
    }

    //! @brief NS::Core::Quaternion を JPH::Quat へ変換する
    [[nodiscard]] inline JPH::Quat ToJolt(const NS::Core::Quaternion& value) noexcept
    {
        return JPH::Quat{value.x, value.y, value.z, value.w};
    }

    //! @brief JPH::Quat を NS::Core::Quaternion へ変換する
    [[nodiscard]] inline NS::Core::Quaternion FromJolt(JPH::QuatArg value) noexcept
    {
        return NS::Core::Quaternion{value.GetX(), value.GetY(), value.GetZ(), value.GetW()};
    }
} // namespace NS::Phys
