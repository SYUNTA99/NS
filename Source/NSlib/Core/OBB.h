#pragma once

#include "NSlib/Core/Math.h"

namespace NS
{
    struct OBB
    {
        Vector3 center{0.0f, 0.0f, 0.0f};
        Vector3 axisX{1.0f, 0.0f, 0.0f};
        Vector3 axisY{0.0f, 1.0f, 0.0f};
        Vector3 axisZ{0.0f, 0.0f, 1.0f};
        float halfExtentX{0.5f};
        float halfExtentY{0.5f};
        float halfExtentZ{0.5f};
    };

    //! rotation で各軸の向きを決め、halfExtents の絶対値を各軸の半幅にする
    [[nodiscard]] OBB MakeOBB(const Vector3& center, const Quaternion& rotation, const Vector3& halfExtents) noexcept;
} // namespace NS
