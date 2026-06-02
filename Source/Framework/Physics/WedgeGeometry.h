#pragma once

/// @file WedgeGeometry.h
/// @brief wedge (楔形スロープ) の衝突三角形生成
///
/// @details 描画 mesh (Graphics::MakeWedge) と同じ規約の collision 版。 SlopeColliderComponent と
/// PlayMode の両経路がこの関数を共有し、 wedge geometry の定義を 1 箇所に集約する
/// 5 面 8 三角形 (斜面 2 + 底面 2 + 裏壁 2 + 左右側面 1+1)、 全て CCW (cross が外向き法線)

#include <array>
#include <cstdint>

#include "Framework/Core/Math.h"
#include "Framework/Physics/SweptTriangle.h"

namespace NS::Physics
{
    /// 中心 center、 半サイズ halfExtents、 傾斜 angleDegrees の wedge を 8 三角形で返す
    /// @param yawRadians Y 軸まわりの向き (連続、 ラジアン)。 0 は +Z 側が高い斜面
    [[nodiscard]] std::array<Triangle, 8> BuildWedgeTriangles(const NS::Core::Vector3& center,
                                                              const NS::Core::Vector3& halfExtents,
                                                              float angleDegrees,
                                                              float yawRadians) noexcept;
} // namespace NS::Physics
