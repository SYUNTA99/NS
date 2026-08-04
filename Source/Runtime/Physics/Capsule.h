#pragma once

#include "Runtime/Math/Math.h"

namespace NS::Physics
{
    /// @brief カプセル形状
    /// @details 円柱の両端に半球がついた形状
    struct Capsule
    {
        NS::Math::Vector3 center{0.0f, 0.0f, 0.0f}; // 中心座標
        NS::Math::Vector3 axis{0.0f, 1.0f, 0.0f};   // カプセルが伸びる方向 (軸)
        float halfHeight = 0.5f;                    // 中心から端の半球の中心までの距離
        float radius = 0.4f;                        // 半径
    };

    /// @brief カプセルの芯線分の両端点
    struct CapsuleSegment
    {
        NS::Math::Vector3 top{0.0f, 0.0f, 0.0f};    // center + 正規化 axis * halfHeight
        NS::Math::Vector3 bottom{0.0f, 0.0f, 0.0f}; // center - 正規化 axis * halfHeight
    };

    /// @brief カプセルの芯線分の両端点を返す
    /// @details axis を正規化してから center ± axis*halfHeight を計算する。零ベクトルは Y 軸にする
    [[nodiscard]] CapsuleSegment CapsuleEndpoints(const NS::Physics::Capsule& capsule) noexcept;

    /// 縦 capsule と AABB の重なり判定。芯線分と box の最短距離が radius 以下なら true
    /// @pre capsule.axis は Y 固定前提
    [[nodiscard]] bool IntersectsCapsuleAABB(const NS::Physics::Capsule& capsule, const NS::Math::AABB& box) noexcept;
} // namespace NS::Physics
