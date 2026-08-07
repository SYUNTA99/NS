#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Physics/SweptTriangle.h"

#include <array>
#include <cstdint>

namespace NS::Physics
{
    /// @brief 中心 center、半サイズ halfExtents、傾斜 angleDegrees の楔形スロープ wedge を 8 三角形で返す
    /// @details Graphics::MakeWedge の描画 mesh と同じ規約で衝突三角形を作る
    /// SlopeColliderComponent がここから衝突三角形を組み、wedge の形の定義を 1 箇所に保つ
    /// 5 面 8 三角形の内訳は斜面 2 + 底面 2 + 裏壁 2 + 左右側面 1+1。全て CCW で cross が外向き法線
    /// @param yawRadians Y 軸まわりの向き(ラジアン、連続値)。0 は +Z 側が高い斜面
    [[nodiscard]] std::array<Triangle, 8> BuildWedgeTriangles(const NS::Core::Vector3& center,
                                                              const NS::Core::Vector3& halfExtents,
                                                              float angleDegrees,
                                                              float yawRadians) noexcept;
} // namespace NS::Physics
