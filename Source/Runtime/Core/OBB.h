#pragma once

#include "Runtime/Core/Math.h"

namespace NS::Core
{
    //! @brief 有向境界ボックス
    //! @details 回転を持つ境界ボックス。中心座標、各軸の向き、中心から各面までの距離で表す
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

    //! @brief 中心・回転・半径から OBB を組む
    //! @details 3 軸は rotation で回した単位軸。半径は絶対値を取るので、負の scale を掛けた値を渡してよい
    //! @param[in] center 中心
    //! @param[in] rotation 3 軸の向き
    //! @param[in] halfExtents 中心から各面までの距離
    //! @return 組み上がった OBB
    [[nodiscard]] OBB MakeOBB(const Vector3& center, const Quaternion& rotation, const Vector3& halfExtents) noexcept;
} // namespace NS::Core
